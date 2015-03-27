#ifndef TRFB_H_INC
#define TRFB_H_INC

#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <c11threads.h>
#include <sys/socket.h>

/* Tiny RFB (VNC) server implementation */

#ifdef __cplusplus
extern "C" {
#endif /* } */

typedef enum trfb_protocol {
	trfb_v3 = 3,
	trfb_v7 = 7,
	trfb_v8 = 8
} trfb_protocol_t;

typedef enum trfb_auth {
	trfb_auth_none
} trfb_auth_t;

typedef struct trfb_format {
	unsigned char bpp, depth;
	unsigned char big_endian;
	unsigned char true_color;
	uint16_t rmax, gmax, bmax;
} trfb_format_t;

typedef struct trfb_client trfb_client_t;
typedef struct trfb_server trfb_server_t;
typedef struct trfb_connection trfb_connection_t;

struct trfb_server {
	int sock;
	thrd_t thread;

#define TRFB_STATE_STOPPED  0x0000
#define TRFB_STATE_WORKING  0x0001
#define TRFB_STATE_STOP     0x0002
#define TRFB_STATE_ERROR    0x8000
	unsigned state;

	size_t width, height;
	/* Pixels array. Always true-color 3 bytes per-pixel. */
	unsigned char *pixels;

	mtx_t lock;

	// struct tv changed; /* last changed */
	// struct tv last_access;

	trfb_connection_t *clients;
};

struct trfb_connection {
	trfb_server_t *server;

	trfb_protocol_t version;

	unsigned state;

	/* Client information */
	struct sockaddr addr;
	socklen_t addrlen;
	char name[64];

	thrd_t thread;
	mtx_t lock;

	/*
	 * Array of pixels. Last state for this client.
	 * Width and height are taken from server
	 */
	unsigned char *pixels;

	int sock;

	trfb_connection_t *next;
};

trfb_server_t *trfb_server_create(size_t width, size_t height);
void trfb_server_destroy(trfb_server_t *server);
int trfb_server_start(trfb_server_t *server);
int trfb_server_stop(trfb_server_t *server);
unsigned trfb_server_get_state(trfb_server_t *S);

/* Set socket to listen: */
int trfb_server_set_socket(trfb_server_t *server, int sock);
/* Bind to specified host and address: */
int trfb_server_bind(trfb_server_t *server, const char *host, const char *port);

/* You can set your own error print function. Default is fwrite(message, strlen(message), 1, stderr). */
extern void (*trfb_log_cb)(const char *message);
/* Internal error message writing function:
 * If message startswith "I:" message is information message.
 * If message startswith "E:" message is error message.
 * If message startswith "W:" message is warning message.
 * Default is error message.
 */
void trfb_msg(const char *fmt, ...);

trfb_connection_t* trfb_connection_create(trfb_server_t *srv, int sock, struct sockaddr *addr, socklen_t addrlen);
void trfb_connection_free(trfb_connection_t *con);

/* Protocol messages: */
ssize_t trfb_send_all(int sock, const void *buf, size_t len);
ssize_t trfb_recv_all(int sock, void *buf, size_t len);

typedef struct trfb_msg_protocol_version {
	trfb_protocol_t proto;
} trfb_msg_protocol_version_t;
int trfb_msg_protocol_version_encode(trfb_msg_protocol_version_t *msg, unsigned char *buf, size_t *len);
int trfb_msg_protocol_version_decode(trfb_msg_protocol_version_t *msg, const unsigned char *buf, size_t len);
int trfb_msg_protocol_version_send(trfb_msg_protocol_version_t *msg, int sock);
int trfb_msg_protocol_version_recv(trfb_msg_protocol_version_t *msg, int sock);

/* extern "C" { */
#ifdef __cplusplus
}
#endif

#endif

