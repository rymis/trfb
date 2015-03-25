#ifndef TRFB_H_INC
#define TRFB_H_INC

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

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

struct trfb_server {
	trfb_protocol_t protocol;
	trfb_auth_t auth;
	int sock;

	size_t width, height;
	unsigned char *pixels;

	// struct tv changed; /* last changed */
	// struct tv last_access;

	trfb_client_t *clients;
};

struct trfb_client {
	trfb_server_t *server;

	int sock;
};

/* extern "C" { */
#ifdef __cplusplus
}
#endif

#endif

