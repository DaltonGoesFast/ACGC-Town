#define _POSIX_C_SOURCE 200112L
/* pc_xport.c - UDP datagrams and WebSocket binary frames for the ACNP protocol.
 *
 * A WebSocket binary frame payload is one ACNP datagram, the same bytes sendto() would
 * carry. The server speaks RFC 6455 just far enough for a browser client: HTTP upgrade,
 * masked client frames, unmasked server frames, ping/pong and close. No fragmentation.
 */
#include "pc_xport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET pc_fd;
#define PC_FD_BAD INVALID_SOCKET
#define pc_close_fd closesocket
#define pc_sock_err() WSAGetLastError()
#define PC_ERR_AGAIN(e) ((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS)
#define PC_ERR_RESET(e) ((e) == WSAECONNRESET || (e) == WSAECONNABORTED)
#define PC_ERR_MSGSIZE(e) ((e) == WSAEMSGSIZE)
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
typedef int pc_fd;
#define PC_FD_BAD (-1)
#define pc_close_fd close
#define pc_sock_err() errno
#define PC_ERR_AGAIN(e) ((e) == EAGAIN || (e) == EWOULDBLOCK || (e) == EINPROGRESS)
#define PC_ERR_RESET(e) ((e) == ECONNRESET || (e) == ECONNABORTED || (e) == EPIPE || (e) == ECONNREFUSED)
#define PC_ERR_MSGSIZE(e) ((e) == EMSGSIZE)
#endif

#define PC_XPORT_WS_MAX 64
#define PC_XPORT_RX     8192
#define PC_XPORT_TX     1200
#define PC_XPORT_INQ    64
#define PC_XPORT_DEADQ  64
#define PC_WS_GUID      "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

enum { WS_FREE = 0, WS_HANDSHAKE = 1, WS_OPEN = 2 };

typedef struct PCXportWs {
    pc_fd    fd;
    int      state;
    uint32_t ip;
    uint16_t port;
    uint8_t  rx[PC_XPORT_RX];
    int      rx_len;
    uint8_t  tx[PC_XPORT_TX];
    int      tx_len;
    int      tx_off;
} PCXportWs;

typedef struct PCXportIn {
    PCXportAddr from;
    int         len;
    uint8_t     data[PC_XPORT_MAX_DGRAM];
} PCXportIn;

static int      s_started = 0;
static pc_fd    s_udp = PC_FD_BAD;
static uint16_t s_udp_port = 0;
static pc_fd    s_ws_listen = PC_FD_BAD;
static uint16_t s_ws_port = 0;
static int      s_ws_is_client = 0;
static PCXportWs s_ws[PC_XPORT_WS_MAX];
static PCXportWs s_ws_client;
static PCXportIn s_inq[PC_XPORT_INQ];
static int      s_inq_head = 0;
static int      s_inq_count = 0;
static PCXportAddr s_deadq[PC_XPORT_DEADQ];
static int      s_dead_head = 0;
static int      s_dead_count = 0;
static uint32_t s_mask_seq = 1;

/* ------------------------------------------------------------------------------------------ */
/* sockets                                                                                     */

static int pc_set_nonblock(pc_fd fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return 0;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0;
#endif
}

static void pc_set_buf(pc_fd fd) {
    int bufsz = 256 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const char*)&bufsz, (int)sizeof(bufsz));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char*)&bufsz, (int)sizeof(bufsz));
}

static uint16_t pc_bound_port(pc_fd fd) {
    struct sockaddr_in addr;
#ifdef _WIN32
    int len = (int)sizeof(addr);
#else
    socklen_t len = (socklen_t)sizeof(addr);
#endif
    memset(&addr, 0, sizeof(addr));
    if (getsockname(fd, (struct sockaddr*)&addr, &len) != 0) return 0;
    return ntohs(addr.sin_port);
}

uint32_t pc_xport_tick_ms(void) {
#ifdef _WIN32
    return GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000u));
#endif
}

uint32_t pc_xport_nonce_mix(void) {
    uint32_t n;
#ifdef _WIN32
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    n = (uint32_t)qpc.LowPart ^ ((uint32_t)qpc.HighPart << 7) ^
        ((uint32_t)GetCurrentProcessId() * 2654435761u) ^ (GetTickCount() << 13);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    n = (uint32_t)ts.tv_nsec ^ ((uint32_t)ts.tv_sec * 2654435761u) ^
        ((uint32_t)getpid() << 13) ^ (pc_xport_tick_ms() << 7);
#endif
    s_mask_seq++;
    n ^= s_mask_seq * 0x9E3779B9u;
    return n != 0 ? n : 1u;
}

int pc_xport_startup(void) {
    if (s_started) return 1;
#ifdef _WIN32
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
    }
#endif
    s_started = 1;
    s_udp = PC_FD_BAD;
    s_ws_listen = PC_FD_BAD;
    memset(s_ws, 0, sizeof(s_ws));
    memset(&s_ws_client, 0, sizeof(s_ws_client));
    s_ws_client.fd = PC_FD_BAD;
    s_inq_head = s_inq_count = 0;
    s_dead_head = s_dead_count = 0;
    s_ws_is_client = 0;
    return 1;
}

static void pc_ws_clear(PCXportWs* c) {
    if (c->fd != PC_FD_BAD) {
        pc_close_fd(c->fd);
        c->fd = PC_FD_BAD;
    }
    c->state = WS_FREE;
    c->rx_len = 0;
    c->tx_len = 0;
    c->tx_off = 0;
    c->ip = 0;
    c->port = 0;
}

void pc_xport_close_sockets(void) {
    int i;
    if (s_udp != PC_FD_BAD) {
        pc_close_fd(s_udp);
        s_udp = PC_FD_BAD;
    }
    s_udp_port = 0;
    if (s_ws_listen != PC_FD_BAD) {
        pc_close_fd(s_ws_listen);
        s_ws_listen = PC_FD_BAD;
    }
    s_ws_port = 0;
    for (i = 0; i < PC_XPORT_WS_MAX; i++) pc_ws_clear(&s_ws[i]);
    pc_ws_clear(&s_ws_client);
    s_ws_is_client = 0;
    s_inq_head = s_inq_count = 0;
    s_dead_head = s_dead_count = 0;
}

void pc_xport_shutdown(void) {
    if (!s_started) return;
    pc_xport_close_sockets();
#ifdef _WIN32
    WSACleanup();
#endif
    s_started = 0;
}

/* ------------------------------------------------------------------------------------------ */
/* addresses                                                                                   */

void pc_xport_addr_udp(PCXportAddr* a, uint32_t ip_nbo, uint16_t port_nbo) {
    memset(a, 0, sizeof(*a));
    a->kind = PC_XPORT_UDP;
    a->ws_id = 0;
    a->ip = ip_nbo;
    a->port = port_nbo;
}

int pc_xport_addr_eq(const PCXportAddr* a, const PCXportAddr* b) {
    if (a == NULL || b == NULL || a->kind != b->kind || a->kind == PC_XPORT_NONE) return 0;
    if (a->kind == PC_XPORT_WS) return a->ws_id == b->ws_id;
    return a->ip == b->ip && a->port == b->port;
}

uint32_t pc_xport_addr_ip(const PCXportAddr* a) {
    return a != NULL ? a->ip : 0;
}

void pc_xport_addr_format(const PCXportAddr* a, char* ip, size_t ipcap, unsigned* port_host) {
    struct in_addr in;
    if (ip != NULL && ipcap > 0) ip[0] = '\0';
    if (port_host != NULL) *port_host = 0;
    if (a == NULL) return;
    in.s_addr = a->ip;
    if (ip != NULL && ipcap > 0) {
        if (a->ip == 0 || inet_ntop(AF_INET, &in, ip, (socklen_t)ipcap) == NULL) {
            snprintf(ip, ipcap, "%s", a->kind == PC_XPORT_WS ? "ws" : "?");
        }
    }
    if (port_host != NULL) *port_host = ntohs(a->port);
}

/* ------------------------------------------------------------------------------------------ */
/* UDP                                                                                         */

int pc_xport_udp_open(void) {
    if (!s_started || s_udp != PC_FD_BAD) return 0;
    s_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_udp == PC_FD_BAD) return 0;
    if (!pc_set_nonblock(s_udp)) {
        pc_close_fd(s_udp);
        s_udp = PC_FD_BAD;
        return 0;
    }
    pc_set_buf(s_udp);
    s_udp_port = 0;
    return 1;
}

int pc_xport_udp_bind(uint16_t port) {
    struct sockaddr_in addr;
    if (!pc_xport_udp_open()) return 0;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(s_udp, (struct sockaddr*)&addr, (int)sizeof(addr)) != 0) {
        pc_close_fd(s_udp);
        s_udp = PC_FD_BAD;
        return 0;
    }
    s_udp_port = pc_bound_port(s_udp);
    return 1;
}

uint16_t pc_xport_udp_port(void) { return s_udp_port; }

static int pc_udp_recv(PCXportAddr* from, void* buf, int cap) {
    struct sockaddr_in addr;
#ifdef _WIN32
    int alen = (int)sizeof(addr);
#else
    socklen_t alen = (socklen_t)sizeof(addr);
#endif
    int n, errors = 0;
    if (s_udp == PC_FD_BAD) return 0;
    for (;;) {
        alen = (int)sizeof(addr);
        memset(&addr, 0, sizeof(addr));
        n = recvfrom(s_udp, (char*)buf, cap, 0, (struct sockaddr*)&addr, &alen);
        if (n < 0) {
            int err = pc_sock_err();
            if (PC_ERR_AGAIN(err)) return 0;
            if ((PC_ERR_RESET(err) || PC_ERR_MSGSIZE(err)) && ++errors < 64) continue;
            return -1;
        }
        if (n == 0) continue;
        pc_xport_addr_udp(from, addr.sin_addr.s_addr, addr.sin_port);
        return n;
    }
}

static int pc_udp_send(const PCXportAddr* to, const void* buf, int len) {
    struct sockaddr_in addr;
    if (s_udp == PC_FD_BAD || to == NULL) return 0;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = to->ip;
    addr.sin_port = to->port;
    return sendto(s_udp, (const char*)buf, len, 0, (struct sockaddr*)&addr, (int)sizeof(addr)) == len;
}

/* ------------------------------------------------------------------------------------------ */
/* SHA-1 + base64 (WebSocket accept key; public-domain FIPS 180-1)                            */

static uint32_t pc_rotl(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void pc_sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
    uint32_t h0 = 0x67452301u, h1 = 0xEFCDAB89u, h2 = 0x98BADCFEu, h3 = 0x10325476u, h4 = 0xC3D2E1F0u;
    uint64_t bits = (uint64_t)len * 8u;
    size_t i, blocks, padded;
    uint8_t* buf;
    padded = len + 1 + 8;
    if (padded % 64) padded += 64 - (padded % 64);
    buf = (uint8_t*)calloc(padded, 1);
    if (buf == NULL) {
        memset(out, 0, 20);
        return;
    }
    memcpy(buf, data, len);
    buf[len] = 0x80;
    for (i = 0; i < 8; i++) buf[padded - 8 + i] = (uint8_t)(bits >> (56 - 8 * (int)i));
    blocks = padded / 64;
    for (i = 0; i < blocks; i++) {
        uint32_t w[80];
        uint32_t a, b, c, d, e, t;
        int t_i;
        const uint8_t* blk = buf + i * 64;
        for (t_i = 0; t_i < 16; t_i++) {
            w[t_i] = ((uint32_t)blk[t_i * 4] << 24) | ((uint32_t)blk[t_i * 4 + 1] << 16) |
                     ((uint32_t)blk[t_i * 4 + 2] << 8) | (uint32_t)blk[t_i * 4 + 3];
        }
        for (t_i = 16; t_i < 80; t_i++) w[t_i] = pc_rotl(w[t_i - 3] ^ w[t_i - 8] ^ w[t_i - 14] ^ w[t_i - 16], 1);
        a = h0; b = h1; c = h2; d = h3; e = h4;
        for (t_i = 0; t_i < 80; t_i++) {
            uint32_t f, k;
            if (t_i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999u; }
            else if (t_i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (t_i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            t = pc_rotl(a, 5) + f + e + k + w[t_i];
            e = d; d = c; c = pc_rotl(b, 30); b = a; a = t;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    free(buf);
    for (i = 0; i < 4; i++) {
        out[i] = (uint8_t)(h0 >> (24 - 8 * i));
        out[4 + i] = (uint8_t)(h1 >> (24 - 8 * i));
        out[8 + i] = (uint8_t)(h2 >> (24 - 8 * i));
        out[12 + i] = (uint8_t)(h3 >> (24 - 8 * i));
        out[16 + i] = (uint8_t)(h4 >> (24 - 8 * i));
    }
}

static void pc_b64(const uint8_t* in, size_t len, char* out, size_t cap) {
    static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0, o = 0;
    while (i < len && o + 4 < cap) {
        unsigned n = (unsigned)in[i++] << 16;
        int rem = 1;
        if (i < len) { n |= (unsigned)in[i++] << 8; rem = 2; }
        if (i < len) { n |= (unsigned)in[i++]; rem = 3; }
        out[o++] = tab[(n >> 18) & 63];
        out[o++] = tab[(n >> 12) & 63];
        out[o++] = rem >= 2 ? tab[(n >> 6) & 63] : '=';
        out[o++] = rem == 3 ? tab[n & 63] : '=';
    }
    if (o < cap) out[o] = '\0';
}

/* ------------------------------------------------------------------------------------------ */
/* WebSocket                                                                                   */

static PCXportWs* pc_ws_slot(uint16_t id) {
    if (id == PC_XPORT_WS_CLIENT_ID) return s_ws_is_client ? &s_ws_client : NULL;
    if (id >= PC_XPORT_WS_MAX) return NULL;
    if (s_ws[id].state == WS_FREE) return NULL;
    return &s_ws[id];
}

static void pc_dead_push(uint16_t id, uint32_t ip, uint16_t port) {
    PCXportAddr* d;
    if (s_dead_count >= PC_XPORT_DEADQ) return;
    d = &s_deadq[(s_dead_head + s_dead_count) % PC_XPORT_DEADQ];
    memset(d, 0, sizeof(*d));
    d->kind = PC_XPORT_WS;
    d->ws_id = id;
    d->ip = ip;
    d->port = port;
    s_dead_count++;
}

static int pc_ws_flush(PCXportWs* c) {
    while (c->tx_off < c->tx_len) {
        int n = send(c->fd, (const char*)c->tx + c->tx_off, c->tx_len - c->tx_off, 0);
        if (n < 0) {
            if (PC_ERR_AGAIN(pc_sock_err())) return 1;
            return 0;
        }
        if (n == 0) return 0;
        c->tx_off += n;
    }
    c->tx_len = 0;
    c->tx_off = 0;
    return 1;
}

static int pc_ws_queue_frame(PCXportWs* c, int opcode, const void* data, int len, int mask) {
    uint8_t hdr[8];
    int hlen = 2;
    uint8_t key[4];
    int i;
    int total;
    if (c == NULL || c->state != WS_OPEN || len < 0 || len > PC_XPORT_MAX_DGRAM) return 0;
    if (c->tx_len > c->tx_off) {
        if (!pc_ws_flush(c)) return 0;
        if (c->tx_len > c->tx_off) return 0;
    }
    hdr[0] = (uint8_t)(0x80 | (opcode & 0x0f));
    if (len < 126) {
        hdr[1] = (uint8_t)len;
    } else {
        hdr[1] = 126;
        hdr[2] = (uint8_t)((len >> 8) & 0xff);
        hdr[3] = (uint8_t)(len & 0xff);
        hlen = 4;
    }
    if (mask) {
        uint32_t r = pc_xport_nonce_mix();
        hdr[1] |= 0x80;
        key[0] = (uint8_t)r; key[1] = (uint8_t)(r >> 8); key[2] = (uint8_t)(r >> 16); key[3] = (uint8_t)(r >> 24);
        memcpy(hdr + hlen, key, 4);
        hlen += 4;
    }
    total = hlen + len;
    if (total > PC_XPORT_TX) return 0;
    memcpy(c->tx, hdr, (size_t)hlen);
    if (len > 0) {
        memcpy(c->tx + hlen, data, (size_t)len);
        if (mask) {
            for (i = 0; i < len; i++) c->tx[hlen + i] ^= key[i & 3];
        }
    }
    c->tx_len = total;
    c->tx_off = 0;
    return pc_ws_flush(c);
}

static int pc_header_get(const char* msg, int len, const char* name, char* out, int cap) {
    int nlen = (int)strlen(name);
    int i = 0;
    while (i + nlen + 1 < len) {
        int line = i;
        int end;
        while (i + 1 < len && !(msg[i] == '\r' && msg[i + 1] == '\n')) i++;
        end = i;
        if (end - line > nlen + 1 && (msg[line + nlen] == ':')) {
            int k, match = 1;
            for (k = 0; k < nlen; k++) {
                if (tolower((unsigned char)msg[line + k]) != tolower((unsigned char)name[k])) match = 0;
            }
            if (match) {
                int v = line + nlen + 1;
                int o = 0;
                while (v < end && (msg[v] == ' ' || msg[v] == '\t')) v++;
                while (v < end && o + 1 < cap) out[o++] = msg[v++];
                out[o] = '\0';
                return 1;
            }
        }
        if (i + 1 < len && msg[i] == '\r') i += 2;
        else break;
    }
    return 0;
}

static int pc_ws_accept_key(const char* client_key, char* out, size_t cap) {
    char joined[128];
    uint8_t dig[20];
    int n;
    n = snprintf(joined, sizeof(joined), "%s%s", client_key, PC_WS_GUID);
    if (n <= 0 || n >= (int)sizeof(joined)) return 0;
    pc_sha1((const uint8_t*)joined, (size_t)n, dig);
    pc_b64(dig, 20, out, cap);
    return out[0] != '\0';
}

static void pc_ws_fail(PCXportWs* c, uint16_t id, int report_dead) {
    uint32_t ip = c->ip;
    uint16_t port = c->port;
    int was_open = c->state == WS_OPEN;
    pc_ws_clear(c);
    if (report_dead && was_open) pc_dead_push(id, ip, port);
}

static int pc_ws_finish_handshake(PCXportWs* c) {
    char key[128], accept[64], resp[256];
    int hdr_end = -1;
    int i, n;
    for (i = 0; i + 3 < c->rx_len; i++) {
        if (c->rx[i] == '\r' && c->rx[i + 1] == '\n' && c->rx[i + 2] == '\r' && c->rx[i + 3] == '\n') {
            hdr_end = i + 4;
            break;
        }
    }
    if (hdr_end < 0) {
        if (c->rx_len > 4096) return 0;
        return 1; /* need more */
    }
    c->rx[hdr_end - 1] = '\0';
    if (!pc_header_get((const char*)c->rx, hdr_end, "Sec-WebSocket-Key", key, (int)sizeof(key))) return 0;
    if (!pc_ws_accept_key(key, accept, sizeof(accept))) return 0;
    n = snprintf(resp, sizeof(resp),
                 "HTTP/1.1 101 Switching Protocols\r\n"
                 "Upgrade: websocket\r\n"
                 "Connection: Upgrade\r\n"
                 "Sec-WebSocket-Accept: %s\r\n"
                 "\r\n",
                 accept);
    if (n <= 0 || n >= (int)sizeof(resp)) return 0;
    {
        int off = 0;
        while (off < n) {
            int w = send(c->fd, resp + off, n - off, 0);
            if (w < 0) return 0;
            if (w == 0) return 0;
            off += w;
        }
    }
    if (hdr_end < c->rx_len) memmove(c->rx, c->rx + hdr_end, (size_t)(c->rx_len - hdr_end));
    c->rx_len -= hdr_end;
    c->state = WS_OPEN;
    return 1;
}

static int pc_inq_push(const PCXportAddr* from, const uint8_t* data, int len) {
    PCXportIn* slot;
    if (len < 0 || len > PC_XPORT_MAX_DGRAM || s_inq_count >= PC_XPORT_INQ) return 0;
    slot = &s_inq[(s_inq_head + s_inq_count) % PC_XPORT_INQ];
    slot->from = *from;
    slot->len = len;
    if (len) memcpy(slot->data, data, (size_t)len);
    s_inq_count++;
    return 1;
}

static int pc_ws_parse(PCXportWs* c, uint16_t id) {
    while (c->state == WS_OPEN && c->rx_len >= 2) {
        int masked, fin, opcode, hlen, plen, i;
        uint8_t key[4];
        uint8_t payload[PC_XPORT_MAX_DGRAM];
        fin = (c->rx[0] & 0x80) != 0;
        opcode = c->rx[0] & 0x0f;
        masked = (c->rx[1] & 0x80) != 0;
        plen = c->rx[1] & 0x7f;
        hlen = 2;
        if (!fin || opcode == 0) return 0; /* no fragmentation */
        if (plen == 127) return 0;
        if (plen == 126) {
            if (c->rx_len < 4) return 1;
            plen = ((int)c->rx[2] << 8) | (int)c->rx[3];
            hlen = 4;
        }
        if (s_ws_is_client) {
            if (masked) return 0; /* server frames are not masked */
        } else if (!masked) {
            return 0; /* client frames must be masked */
        }
        if (masked) {
            if (c->rx_len < hlen + 4) return 1;
            memcpy(key, c->rx + hlen, 4);
            hlen += 4;
        }
        if (plen > PC_XPORT_MAX_DGRAM) return 0;
        if (c->rx_len < hlen + plen) return 1;
        for (i = 0; i < plen; i++) {
            uint8_t b = c->rx[hlen + i];
            if (masked) b ^= key[i & 3];
            payload[i] = b;
        }
        memmove(c->rx, c->rx + hlen + plen, (size_t)(c->rx_len - (hlen + plen)));
        c->rx_len -= hlen + plen;
        if (opcode == 0x8) return 0; /* close */
        if (opcode == 0x9) {
            if (!pc_ws_queue_frame(c, 0xA, payload, plen, s_ws_is_client)) return 0;
            continue;
        }
        if (opcode == 0xA) continue;
        if (opcode != 0x2) return 0; /* binary only */
        {
            PCXportAddr from;
            memset(&from, 0, sizeof(from));
            from.kind = PC_XPORT_WS;
            from.ws_id = id;
            from.ip = c->ip;
            from.port = c->port;
            if (!pc_inq_push(&from, payload, plen)) return 1; /* queue full: leave the frame consumed; drop */
        }
    }
    return 1;
}

static void pc_ws_read(PCXportWs* c, uint16_t id) {
    if (c->state == WS_FREE || c->fd == PC_FD_BAD) return;
    if (c->tx_len > c->tx_off && !pc_ws_flush(c)) {
        pc_ws_fail(c, id, 1);
        return;
    }
    for (;;) {
        int space = PC_XPORT_RX - c->rx_len;
        int n;
        if (space <= 0) {
            pc_ws_fail(c, id, 1);
            return;
        }
        n = recv(c->fd, (char*)c->rx + c->rx_len, space, 0);
        if (n < 0) {
            if (PC_ERR_AGAIN(pc_sock_err())) break;
            pc_ws_fail(c, id, 1);
            return;
        }
        if (n == 0) {
            pc_ws_fail(c, id, 1);
            return;
        }
        c->rx_len += n;
        if (c->state == WS_HANDSHAKE) {
            if (!pc_ws_finish_handshake(c)) {
                pc_ws_fail(c, id, 0);
                return;
            }
        }
        if (c->state == WS_OPEN && !pc_ws_parse(c, id)) {
            pc_ws_fail(c, id, 1);
            return;
        }
        if (n < space) break;
    }
}

static void pc_ws_accept(void) {
    if (s_ws_listen == PC_FD_BAD) return;
    for (;;) {
        struct sockaddr_in addr;
#ifdef _WIN32
        int alen = (int)sizeof(addr);
#else
        socklen_t alen = (socklen_t)sizeof(addr);
#endif
        pc_fd fd;
        int slot = -1;
        int i;
        int one = 1;
        alen = (int)sizeof(addr);
        fd = accept(s_ws_listen, (struct sockaddr*)&addr, &alen);
        if (fd == PC_FD_BAD) return;
        for (i = 0; i < PC_XPORT_WS_MAX; i++) {
            if (s_ws[i].state == WS_FREE) { slot = i; break; }
        }
        if (slot < 0) {
            pc_close_fd(fd);
            continue;
        }
        pc_set_nonblock(fd);
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, (int)sizeof(one));
        memset(&s_ws[slot], 0, sizeof(s_ws[slot]));
        s_ws[slot].fd = fd;
        s_ws[slot].state = WS_HANDSHAKE;
        s_ws[slot].ip = addr.sin_addr.s_addr;
        s_ws[slot].port = addr.sin_port;
    }
}

static void pc_ws_service(void) {
    int i;
    pc_ws_accept();
    for (i = 0; i < PC_XPORT_WS_MAX; i++) {
        if (s_ws[i].state != WS_FREE) pc_ws_read(&s_ws[i], (uint16_t)i);
    }
    if (s_ws_is_client && s_ws_client.state != WS_FREE) pc_ws_read(&s_ws_client, PC_XPORT_WS_CLIENT_ID);
}

uint16_t pc_xport_ws_port(void) { return s_ws_port; }

int pc_xport_ws_listen(uint16_t port) {
    struct sockaddr_in addr;
    int one = 1;
    if (!s_started || s_ws_listen != PC_FD_BAD || s_ws_is_client) return 0;
    s_ws_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_ws_listen == PC_FD_BAD) return 0;
    setsockopt(s_ws_listen, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, (int)sizeof(one));
    if (!pc_set_nonblock(s_ws_listen)) {
        pc_close_fd(s_ws_listen);
        s_ws_listen = PC_FD_BAD;
        return 0;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(s_ws_listen, (struct sockaddr*)&addr, (int)sizeof(addr)) != 0 ||
        listen(s_ws_listen, 16) != 0) {
        pc_close_fd(s_ws_listen);
        s_ws_listen = PC_FD_BAD;
        return 0;
    }
    s_ws_port = pc_bound_port(s_ws_listen);
    s_ws_is_client = 0;
    return 1;
}

static int pc_resolve(const char* host, struct in_addr* out) {
    struct addrinfo hints, *res = NULL;
    if (inet_pton(AF_INET, host, out) == 1) return 1;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || res == NULL) return 0;
    if (res->ai_family == AF_INET) *out = ((struct sockaddr_in*)res->ai_addr)->sin_addr;
    else { freeaddrinfo(res); return 0; }
    freeaddrinfo(res);
    return 1;
}

static int pc_send_all_blocking(pc_fd fd, const char* buf, int len) {
    int off = 0;
    while (off < len) {
        int n = send(fd, buf + off, len - off, 0);
        if (n < 0) {
            if (PC_ERR_AGAIN(pc_sock_err())) continue;
            return 0;
        }
        if (n == 0) return 0;
        off += n;
    }
    return 1;
}

static int pc_recv_until_headers(pc_fd fd, char* buf, int cap) {
    int len = 0;
    while (len + 1 < cap) {
        int n = recv(fd, buf + len, cap - 1 - len, 0);
        int i;
        if (n < 0) {
            if (PC_ERR_AGAIN(pc_sock_err())) continue;
            return -1;
        }
        if (n == 0) return -1;
        len += n;
        buf[len] = '\0';
        for (i = 0; i + 3 < len; i++) {
            if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') return len;
        }
    }
    return -1;
}

int pc_xport_resolve_udp(const char* host, uint16_t port_host, PCXportAddr* out) {
    struct in_addr ip;
    if (host == NULL || out == NULL || !pc_resolve(host, &ip)) return 0;
    pc_xport_addr_udp(out, ip.s_addr, htons(port_host));
    return 1;
}

int pc_xport_ws_connect(const char* host, uint16_t port, PCXportAddr* out) {
    struct sockaddr_in addr;
    struct in_addr ip;
    char req[256], resp[1024], key_raw[16], key_b64[32], expect[64], got[64];
    int i;
    uint32_t r;
    if (!s_started || host == NULL || out == NULL || s_ws_listen != PC_FD_BAD || s_ws_is_client) return 0;
    if (!pc_resolve(host, &ip)) return 0;
    memset(&s_ws_client, 0, sizeof(s_ws_client));
    s_ws_client.fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_ws_client.fd == PC_FD_BAD) return 0;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr = ip;
    addr.sin_port = htons(port);
    if (connect(s_ws_client.fd, (struct sockaddr*)&addr, (int)sizeof(addr)) != 0) {
        pc_ws_clear(&s_ws_client);
        return 0;
    }
    r = pc_xport_nonce_mix();
    for (i = 0; i < 16; i++) key_raw[i] = (uint8_t)(r >> ((i % 4) * 8)) ^ (uint8_t)(i * 17);
    pc_b64((const uint8_t*)key_raw, 16, key_b64, sizeof(key_b64));
    snprintf(req, sizeof(req),
             "GET /acgc HTTP/1.1\r\n"
             "Host: %s:%u\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: %s\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "\r\n",
             host, (unsigned)port, key_b64);
    if (!pc_send_all_blocking(s_ws_client.fd, req, (int)strlen(req))) {
        pc_ws_clear(&s_ws_client);
        return 0;
    }
    if (pc_recv_until_headers(s_ws_client.fd, resp, (int)sizeof(resp)) < 0) {
        pc_ws_clear(&s_ws_client);
        return 0;
    }
    if (strncmp(resp, "HTTP/1.1 101", 12) != 0 && strncmp(resp, "HTTP/1.0 101", 12) != 0) {
        pc_ws_clear(&s_ws_client);
        return 0;
    }
    if (!pc_ws_accept_key(key_b64, expect, sizeof(expect)) ||
        !pc_header_get(resp, (int)strlen(resp), "Sec-WebSocket-Accept", got, (int)sizeof(got)) ||
        strcmp(expect, got) != 0) {
        pc_ws_clear(&s_ws_client);
        return 0;
    }
    if (!pc_set_nonblock(s_ws_client.fd)) {
        pc_ws_clear(&s_ws_client);
        return 0;
    }
    s_ws_client.state = WS_OPEN;
    s_ws_client.ip = ip.s_addr;
    s_ws_client.port = htons(port);
    s_ws_is_client = 1;
    memset(out, 0, sizeof(*out));
    out->kind = PC_XPORT_WS;
    out->ws_id = PC_XPORT_WS_CLIENT_ID;
    out->ip = s_ws_client.ip;
    out->port = s_ws_client.port;
    return 1;
}

static int pc_ws_send(const PCXportAddr* to, const void* buf, int len) {
    PCXportWs* c = pc_ws_slot(to->ws_id);
    if (c == NULL || c->state != WS_OPEN) return 0;
    return pc_ws_queue_frame(c, 0x2, buf, len, s_ws_is_client && to->ws_id == PC_XPORT_WS_CLIENT_ID);
}

void pc_xport_close(const PCXportAddr* addr) {
    PCXportWs* c;
    if (addr == NULL || addr->kind != PC_XPORT_WS) return;
    c = pc_ws_slot(addr->ws_id);
    if (c == NULL) return;
    pc_ws_clear(c);
    if (addr->ws_id == PC_XPORT_WS_CLIENT_ID) s_ws_is_client = 0;
}

int pc_xport_take_dead(PCXportAddr* dead) {
    if (s_dead_count == 0 || dead == NULL) return 0;
    *dead = s_deadq[s_dead_head];
    s_dead_head = (s_dead_head + 1) % PC_XPORT_DEADQ;
    s_dead_count--;
    return 1;
}

int pc_xport_send(const PCXportAddr* to, const void* buf, int len) {
    if (to == NULL || buf == NULL || len < 0 || len > PC_XPORT_MAX_DGRAM) return 0;
    if (to->kind == PC_XPORT_UDP) return pc_udp_send(to, buf, len);
    if (to->kind == PC_XPORT_WS) return pc_ws_send(to, buf, len);
    return 0;
}

int pc_xport_ws_accept_test(const char* client_key, char* out, size_t cap) {
    if (client_key == NULL || out == NULL) return 0;
    return pc_ws_accept_key(client_key, out, cap);
}

int pc_xport_recv(PCXportAddr* from, void* buf, int cap) {
    int n;
    if (from == NULL || buf == NULL || cap <= 0) return -1;
    n = pc_udp_recv(from, buf, cap);
    if (n > 0) return n;
    pc_ws_service();
    if (s_inq_count == 0) return n < 0 ? n : 0;
    {
        PCXportIn* slot = &s_inq[s_inq_head];
        int copy = slot->len;
        if (copy > cap) copy = cap;
        *from = slot->from;
        if (copy > 0) memcpy(buf, slot->data, (size_t)copy);
        s_inq_head = (s_inq_head + 1) % PC_XPORT_INQ;
        s_inq_count--;
        return copy;
    }
}
