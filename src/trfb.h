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

#define TRFB_EOF    0xffff
#define TRFB_BUFSIZ 2048
typedef struct trfb_io {
	void *ctx;
	int error;
	/* read/write functions must not touch buf, pos and len! */
	unsigned char rbuf[TRFB_BUFSIZ];
	size_t rlen, rpos;
	unsigned char wbuf[TRFB_BUFSIZ];
	size_t wlen;

	void (*free)(void *ctx);
	/* This I/O functions must process I/O operation and return:
	 *   count of bytes processed on success
	 *   0 on timeout
	 *   -1 on error (and closed connection)
	 */
	ssize_t (*read)(struct trfb_io *io, void *buf, ssize_t len, unsigned timeout);
	ssize_t (*write)(struct trfb_io *io, const void *buf, ssize_t len, unsigned timeout);
} trfb_io_t;

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
	unsigned char rshift, gshift, bshift;
} trfb_format_t;

typedef struct trfb_framebuffer {
	mtx_t lock;

	unsigned width, height;
	/**
	 * Bytes per pixel. Supported values are:
	 *   8 - and in this case this is 256-color image. If rmask == 0 library will use pallete in other cases it is TrueColor
	 *   16 - pixels is uint16_t*
	 *   32 - pixels is uint32_t*
	 */
	unsigned char bpp;

	uint32_t rmask, gmask, bmask;
	unsigned char rshift, gshift, bshift;

	void *pixels;
} trfb_framebuffer_t;

#define TRFB_FB16_RMASK  0x1f
#define TRFB_FB16_GMASK  0x3f
#define TRFB_FB16_BMASK  0x1f

#define TRFB_FB16_RSHIFT 11
#define TRFB_FB16_GSHIFT  5
#define TRFB_FB16_BSHIFT  0

#define TRFB_FB32_RMASK  0xff
#define TRFB_FB32_GMASK  0xff
#define TRFB_FB32_BMASK  0xff

#define TRFB_FB32_RSHIFT 16
#define TRFB_FB32_GSHIFT  8
#define TRFB_FB32_BSHIFT  0

typedef uint32_t trfb_color_t;
#define TRFB_RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))
#define TRFB_COLOR_R(col) (((col) >> TRFB_FB32_RSHIFT) & TRFB_FB32_RMASK)
#define TRFB_COLOR_G(col) (((col) >> TRFB_FB32_GSHIFT) & TRFB_FB32_GMASK)
#define TRFB_COLOR_B(col) (((col) >> TRFB_FB32_BSHIFT) & TRFB_FB32_BMASK)

extern const trfb_color_t trfb_framebuffer_pallete[256];
static inline trfb_color_t trfb_fb_get_pixel(trfb_framebuffer_t *fb, unsigned x, unsigned y)
{
	return trfb_framebuffer_pallete[((uint8_t*)fb->pixels)[y * fb->width + x]];
}

extern const uint8_t trfb_framebuffer_revert_pallete[256];
static inline void trfb_fb_set_pixel(trfb_framebuffer_t *fb, unsigned x, unsigned y, trfb_color_t col)
{
	((uint8_t*)fb->pixels)[y * fb->width + x] =
		trfb_framebuffer_revert_pallete[TRFB_COLOR_R(col)] * 36 +
		trfb_framebuffer_revert_pallete[TRFB_COLOR_G(col)] * 6 +
		trfb_framebuffer_revert_pallete[TRFB_COLOR_B(col)];
}

static inline trfb_color_t trfb_fb8_get_pixel(trfb_framebuffer_t *fb, unsigned x, unsigned y)
{
	register trfb_color_t c = ((uint8_t*)fb->pixels)[y * fb->width + x];
	
	return TRFB_RGB(
			(((c >> fb->rshift) << 8) / (fb->rmask + 1)),
			(((c >> fb->gshift) << 8) / (fb->gmask + 1)),
			(((c >> fb->bshift) << 8) / (fb->bmask + 1))
		       );
}

static inline void trfb_fb8_set_pixel(trfb_framebuffer_t *fb, unsigned x, unsigned y, trfb_color_t col)
{
	((uint8_t*)fb->pixels)[y * fb->width + x] =
		((TRFB_COLOR_R(col) * (fb->rmask + 1) / 256) << fb->rshift) |
		((TRFB_COLOR_G(col) * (fb->gmask + 1) / 256) << fb->gshift) |
		((TRFB_COLOR_B(col) * (fb->bmask + 1) / 256) << fb->bshift);
}

static inline trfb_color_t trfb_fb16_get_pixel(trfb_framebuffer_t *fb, unsigned x, unsigned y)
{
	register trfb_color_t c = ((uint16_t*)fb->pixels)[y * fb->width + x];

	return TRFB_RGB(
			(((c >> fb->rshift) << 8) / (fb->rmask + 1)),
			(((c >> fb->gshift) << 8) / (fb->gmask + 1)),
			(((c >> fb->bshift) << 8) / (fb->bmask + 1))
		       );
}

static inline void trfb_fb16_set_pixel(trfb_framebuffer_t *fb, unsigned x, unsigned y, trfb_color_t col)
{
	((uint16_t*)fb->pixels)[y * fb->width + x] =
		((TRFB_COLOR_R(col) * (fb->rmask + 1) / 256) << fb->rshift) |
		((TRFB_COLOR_G(col) * (fb->gmask + 1) / 256) << fb->gshift) |
		((TRFB_COLOR_B(col) * (fb->bmask + 1) / 256) << fb->bshift);
}

static inline trfb_color_t trfb_fb32_get_pixel(trfb_framebuffer_t *fb, unsigned x, unsigned y)
{
	trfb_color_t c = ((uint32_t*)fb->pixels)[y * fb->width + x];
	return TRFB_RGB(
			((c >> fb->rshift) & fb->rmask),
			((c >> fb->gshift) & fb->gmask),
			((c >> fb->bshift) & fb->bmask));
}

static inline void trfb_fb32_set_pixel(trfb_framebuffer_t *fb, unsigned x, unsigned y, trfb_color_t col)
{
	((uint32_t*)fb->pixels)[y * fb->width + x] =
		(TRFB_COLOR_R(col) << fb->rshift) |
		(TRFB_COLOR_G(col) << fb->gshift) |
		(TRFB_COLOR_B(col) << fb->bshift);
}

static inline trfb_color_t trfb_framebuffer_get_pixel(trfb_framebuffer_t *fb, unsigned x, unsigned y)
{
	if (fb->bpp == 1) {
		if (fb->rmask) {
			return trfb_fb8_get_pixel(fb, x, y);
		} else {
			return trfb_fb_get_pixel(fb, x, y);
		}
	} else if (fb->bpp == 2) {
		return trfb_fb16_get_pixel(fb, x, y);
	} else if (fb->bpp == 4) {
		return trfb_fb32_get_pixel(fb, x, y);
	}
	return 0;
}

static inline void trfb_framebuffer_set_pixel(trfb_framebuffer_t *fb, unsigned x, unsigned y, trfb_color_t col)
{
	if (fb->bpp == 1)
		trfb_fb_set_pixel(fb, x, y, col);
	else if (fb->bpp == 2)
		trfb_fb16_set_pixel(fb, x, y, col);
	else if (fb->bpp == 4)
		trfb_fb32_set_pixel(fb, x, y, col);
	return;
}

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

	trfb_framebuffer_t *fb;
	unsigned updated;

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
	trfb_framebuffer_t *fb;
	trfb_format_t format;

	trfb_io_t *io;

	trfb_connection_t *next;
};

trfb_server_t *trfb_server_create(size_t width, size_t height, unsigned bpp);
void trfb_server_destroy(trfb_server_t *server);
int trfb_server_start(trfb_server_t *server);
int trfb_server_stop(trfb_server_t *server);
unsigned trfb_server_get_state(trfb_server_t *S);

int trfb_server_lock_fb(trfb_server_t *srv, int w);
int trfb_server_unlock_fb(trfb_server_t *srv);

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
/* I/O functions capable to stop thread when you need it */
ssize_t trfb_connection_read(trfb_connection_t *con, void *buf, ssize_t len);
void trfb_connection_read_all(trfb_connection_t *con, void *buf, ssize_t len);
ssize_t trfb_connection_write(trfb_connection_t *con, const void *buf, ssize_t len);
void trfb_connection_write_all(trfb_connection_t *con, const void *buf, ssize_t len);
void trfb_connection_flush(trfb_connection_t *con);

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

trfb_io_t* trfb_io_socket_wrap(int sock);

/* I/O functions: */
ssize_t trfb_io_read(trfb_io_t *io, void *buf, ssize_t len, unsigned timeout);
ssize_t trfb_io_write(trfb_io_t *io, const void *buf, ssize_t len, unsigned timeout);
void trfb_io_free(trfb_io_t *io);
int trfb_io_flush(trfb_io_t *io, unsigned timeout);
int trfb_io_fgetc(trfb_io_t *io, unsigned timeout);
int trfb_io_fputc(unsigned char c, trfb_io_t *io, unsigned timeout);

#define trfb_io_getc(io, timeout) (((io)->rpos < (io)->rlen)? (io)->rbuf[(io)->rpos++]: trfb_io_fgetc(io, timeout))
#define trfb_io_putc(c, io, timeout) (((io)->wlen < TRFB_BUFSIZ)? ((io)->wbuf[(io)->wlen++] = (c)): trfb_io_fputc(c, io, timeout))

trfb_framebuffer_t* trfb_framebuffer_create(unsigned width, unsigned height, unsigned char bpp);
trfb_framebuffer_t* trfb_framebuffer_create_of_format(unsigned width, unsigned height, trfb_format_t *fmt);
void trfb_framebuffer_free(trfb_framebuffer_t *fb);
int trfb_framebuffer_resize(trfb_framebuffer_t *fb, unsigned width, unsigned height);
int trfb_framebuffer_convert(trfb_framebuffer_t *dst, trfb_framebuffer_t *src);
int trfb_framebuffer_format(trfb_framebuffer_t *fb, trfb_format_t *fmt);
void trfb_framebuffer_endian(trfb_framebuffer_t *fb, int is_be);
trfb_framebuffer_t* trfb_framebuffer_copy(trfb_framebuffer_t *fb);

/* extern "C" { */
#ifdef __cplusplus
}
#endif

#endif

