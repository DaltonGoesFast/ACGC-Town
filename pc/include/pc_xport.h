/* pc_xport.h - datagram transport under the ACNP protocol (pc_net.c).
 *
 * One datagram here is exactly one UDP payload: the 8-byte ACNP header plus its body
 * (at most PC_XPORT_MAX_DGRAM bytes). WebSocket carries that same byte string as a single
 * binary frame (RFC 6455). The reliability, handshake and peer table in pc_net.c do not
 * change with the transport.
 *
 * UDP peers are identified by IPv4 address + port. WebSocket peers are identified by a
 * connection id (the TCP session). A browser cannot send raw UDP, so the dedicated server
 * listens on both.
 */
#ifndef PC_XPORT_H
#define PC_XPORT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC_XPORT_NONE 0
#define PC_XPORT_UDP  1
#define PC_XPORT_WS   2

/* RDATA ceiling used by pc_net.c: 8 byte header + 4 byte seq + 1024 payload. */
#define PC_XPORT_MAX_DGRAM 1036

/* Outbound client WebSocket (not a row in the server's accept table). */
#define PC_XPORT_WS_CLIENT_ID 0xFFFEu

typedef struct PCXportAddr {
    uint8_t  kind;  /* PC_XPORT_UDP or PC_XPORT_WS */
    uint8_t  _pad;
    uint16_t ws_id; /* WebSocket connection id; PC_XPORT_WS_CLIENT_ID for the outbound client */
    uint32_t ip;    /* IPv4, network byte order; 0 if unknown */
    uint16_t port;  /* UDP port or remote TCP port, network byte order */
    uint16_t _pad2;
} PCXportAddr;

int      pc_xport_startup(void);
void     pc_xport_shutdown(void);
/* Close UDP and WebSocket sockets but leave process-wide socket startup in place. */
void     pc_xport_close_sockets(void);

int      pc_xport_udp_open(void);
int      pc_xport_udp_bind(uint16_t port);
uint16_t pc_xport_udp_port(void); /* host order; 0 if UDP is not open */

/* Host: TCP listen + HTTP upgrade. `port` 0 lets the OS pick. */
int      pc_xport_ws_listen(uint16_t port);
uint16_t pc_xport_ws_port(void); /* host order; 0 if not listening */

/* Client: blocking TCP connect + WebSocket upgrade, then non-blocking frames.
 * On success `out` addresses the single outbound connection. */
int      pc_xport_ws_connect(const char* host, uint16_t port, PCXportAddr* out);
/* Resolve host to an IPv4 UDP endpoint (blocks on DNS). 1 = ok. */
int      pc_xport_resolve_udp(const char* host, uint16_t port_host, PCXportAddr* out);

int      pc_xport_send(const PCXportAddr* to, const void* buf, int len);
/* >0 bytes, 0 nothing waiting, -1 hard error. Never blocks. */
int      pc_xport_recv(PCXportAddr* from, void* buf, int cap);
/* A WebSocket peer whose TCP session closed. 1 = one address written to *dead. */
int      pc_xport_take_dead(PCXportAddr* dead);
void     pc_xport_close(const PCXportAddr* addr);

int      pc_xport_addr_eq(const PCXportAddr* a, const PCXportAddr* b);
uint32_t pc_xport_addr_ip(const PCXportAddr* a);
void     pc_xport_addr_format(const PCXportAddr* a, char* ip, size_t ipcap, unsigned* port_host);
void     pc_xport_addr_udp(PCXportAddr* a, uint32_t ip_nbo, uint16_t port_nbo);

uint32_t pc_xport_tick_ms(void);
uint32_t pc_xport_nonce_mix(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_XPORT_H */
