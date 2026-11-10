/* xport_selftest.c - UDP + WebSocket datagram transport, no disc image and no game.
 *
 * Build: gcc -std=c11 -O2 -Ipc/include -o xport_selftest pc/tools/xport_selftest.c
 *        pc/src/pc_xport.c pc/src/pc_net.c pc/src/pc_peer_table.c -pthread
 */
#define _DEFAULT_SOURCE
#include "pc_xport.h"
#include "pc_net.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern int pc_xport_ws_accept_test(const char* client_key, char* out, size_t cap);

static int g_fails = 0;

static void expect(int cond, const char* msg) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fails++;
    } else {
        printf("ok   %s\n", msg);
    }
}

static int send_all(int fd, const void* buf, int len) {
    int off = 0;
    while (off < len) {
        int n = (int)send(fd, (const char*)buf + off, (size_t)(len - off), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        if (n == 0) return 0;
        off += n;
    }
    return 1;
}

static int recv_until(int fd, char* buf, int cap, const char* needle) {
    int len = 0;
    while (len + 1 < cap) {
        int n = (int)recv(fd, buf + len, (size_t)(cap - 1 - len), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        len += n;
        buf[len] = '\0';
        if (strstr(buf, needle) != NULL) return len;
    }
    return -1;
}

static void test_rfc_accept(void) {
    char out[64];
    expect(pc_xport_ws_accept_test("dGhlIHNhbXBsZSBub25jZQ==", out, sizeof(out)), "accept computed");
    expect(strcmp(out, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0, "RFC 6455 Sec-WebSocket-Accept vector");
}

/* Same-process WebSocket: raw TCP client, server side is pc_xport. */
static void test_ws_frame(void) {
    struct sockaddr_in addr;
    int fd, one = 1;
    char req[256], resp[1024], got[64];
    uint8_t frame[16];
    uint8_t buf[64];
    PCXportAddr from, back;
    int n, i;
    uint16_t port;
    const char* payload = "hi";

    expect(pc_xport_startup(), "xport startup");
    expect(pc_xport_ws_listen(0), "ws listen ephemeral");
    port = pc_xport_ws_port();
    expect(port != 0, "ws port assigned");

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    expect(fd >= 0, "client socket");
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    expect(connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0, "tcp connect");
    (void)one;
    snprintf(req, sizeof(req),
             "GET /acgc HTTP/1.1\r\n"
             "Host: 127.0.0.1:%u\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "\r\n",
             (unsigned)port);
    expect(send_all(fd, req, (int)strlen(req)), "upgrade request");
    /* The server completes the handshake inside recv. Drive it, then read the 101. */
    for (i = 0; i < 50; i++) {
        n = pc_xport_recv(&from, buf, (int)sizeof(buf));
        if (n < 0) break;
        usleep(2000);
    }
    expect(recv_until(fd, resp, (int)sizeof(resp), "\r\n\r\n") > 0, "101 response");
    expect(strncmp(resp, "HTTP/1.1 101", 12) == 0, "switching protocols");
    expect(strstr(resp, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != NULL, "accept header matches RFC");

    /* Masked binary frame, payload "hi". */
    frame[0] = 0x82;
    frame[1] = 0x82; /* masked, len 2 */
    frame[2] = 0x11; frame[3] = 0x22; frame[4] = 0x33; frame[5] = 0x44;
    frame[6] = (uint8_t)('h' ^ 0x11);
    frame[7] = (uint8_t)('i' ^ 0x22);
    expect(send_all(fd, frame, 8), "send binary frame");
    n = 0;
    memset(&from, 0, sizeof(from));
    for (i = 0; i < 50 && n <= 0; i++) {
        n = pc_xport_recv(&from, buf, (int)sizeof(buf));
        if (n == 0) usleep(2000);
    }
    expect(n == 2 && memcmp(buf, payload, 2) == 0, "server received unmasked payload");
    expect(from.kind == PC_XPORT_WS, "peer is websocket");

    back = from;
    expect(pc_xport_send(&back, "yo", 2), "server sends binary frame");
    {
        uint8_t rb[16];
        int gotn = 0;
        while (gotn < 4) {
            int r = (int)recv(fd, rb + gotn, (size_t)(sizeof(rb) - (size_t)gotn), 0);
            if (r <= 0) break;
            gotn += r;
        }
        expect(gotn >= 4 && rb[0] == 0x82 && (rb[1] & 0x7f) == 2 && rb[2] == 'y' && rb[3] == 'o',
               "client received unmasked yo");
    }
    (void)got;
    close(fd);
    pc_xport_shutdown();
}

static int client_proc(int use_ws, uint16_t udp, uint16_t ws) {
    int ms = 0, connected = 0, sent = 0, got = 0;
    if (!pc_net_init()) return 2;
    if (use_ws) {
        int tries = 0;
        while (tries < 50 && !pc_net_client_connect_ws("127.0.0.1", ws)) {
            usleep(20000);
            tries++;
        }
        if (tries >= 50) return 2;
    } else {
        usleep(50000);
        if (!pc_net_client_connect("127.0.0.1", udp)) return 2;
    }
    while (ms < 4000) {
        PCNetEvent ev;
        pc_net_poll();
        while (pc_net_next_event(&ev)) {
            if (ev.type == PC_NET_EVENT_PEER_CONNECTED) connected = 1;
            if (ev.type == PC_NET_EVENT_DATA && ev.size == 3 && memcmp(ev.data, "ACK", 3) == 0) got = 1;
        }
        if (connected && !sent) {
            if (pc_net_send(0, PC_NET_RELIABLE, "PING", 4)) sent = 1;
        }
        if (got) break;
        usleep(10000);
        ms += 10;
    }
    pc_net_disconnect(PC_NET_INVALID_PEER);
    pc_net_shutdown();
    return (connected && sent && got) ? 0 : 3;
}

static void test_protocol(int use_ws) {
    uint16_t udp = (uint16_t)(30000 + (getpid() % 10000));
    uint16_t ws = (uint16_t)(udp + 1);
    pid_t pid;
    int status = -1, got = 0, ms = 0;
    char label[64];
    snprintf(label, sizeof(label), "%s ACNP ping/ack", use_ws ? "websocket" : "udp");

    pid = fork();
    if (pid < 0) {
        expect(0, "fork");
        return;
    }
    if (pid == 0) {
        int rc = client_proc(use_ws, udp, ws);
        _exit(rc);
    }
    if (!pc_net_init()) {
        kill(pid, SIGTERM);
        waitpid(pid, &status, 0);
        expect(0, "host init");
        return;
    }
    pc_net_set_peer_capacity(4);
    pc_net_set_ws_port(use_ws ? ws : 0);
    if (!pc_net_host_start(udp)) {
        /* Port in use: one retry on a different pair. The child is already bound to the old ports. */
        kill(pid, SIGTERM);
        waitpid(pid, &status, 0);
        pc_net_shutdown();
        expect(0, "host bind");
        return;
    }
    while (ms < 4000) {
        PCNetEvent ev;
        pc_net_poll();
        while (pc_net_next_event(&ev)) {
            if (ev.type == PC_NET_EVENT_DATA && ev.size == 4 && memcmp(ev.data, "PING", 4) == 0) {
                pc_net_send(ev.peer, PC_NET_RELIABLE, "ACK", 3);
                got = 1;
            }
        }
        if (got) break;
        usleep(10000);
        ms += 10;
    }
    /* Give the client a moment to observe ACK and exit. */
    {
        int waited = 0;
        while (waited < 1000) {
            int w = waitpid(pid, &status, WNOHANG);
            if (w == pid) break;
            pc_net_poll();
            usleep(10000);
            waited += 10;
        }
        if (waited >= 1000) {
            kill(pid, SIGTERM);
            waitpid(pid, &status, 0);
        }
    }
    pc_net_shutdown();
    expect(got && WIFEXITED(status) && WEXITSTATUS(status) == 0, label);
}

int main(void) {
    test_rfc_accept();
    test_ws_frame();
    test_protocol(0);
    test_protocol(1);
    if (g_fails) {
        fprintf(stderr, "%d failure(s)\n", g_fails);
        return 1;
    }
    printf("all transport tests passed\n");
    return 0;
}
