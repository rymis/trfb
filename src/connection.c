#include <trfb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#include <netdb.h>

static int connection(void *con_in);

void trfb_connection_free(trfb_connection_t *con)
{
	trfb_framebuffer_free(con->fb);
	trfb_io_free(con->io);
	mtx_destroy(&con->lock);
	free(con);
}

trfb_connection_t* trfb_connection_create(trfb_server_t *srv, int sock, struct sockaddr *addr, socklen_t addrlen)
{
	trfb_connection_t *C = calloc(1, sizeof(trfb_connection_t));
	char host[NI_MAXHOST];
	char port[NI_MAXSERV];
	int rv;

	if (!C) {
		trfb_msg("Not enought memory to create connection");
		return NULL;
	}

	if (addrlen > sizeof(struct sockaddr)) {
		trfb_msg("Fatal error: addrlen > sizeof(struct sockaddr)");// (%d, %d)", (int)addrlen, (int)sizeof(struct sockaddr));
		free(C);
		return NULL;
	}

	C->io = trfb_io_socket_wrap(sock);
	if (!C->io) {
		free(C);
		trfb_msg("Can not wrap socket");
		return NULL;
	}

	C->fb = NULL;
	C->server = srv;

	mtx_init(&C->lock, mtx_plain);
	C->next = NULL;
	C->state = TRFB_STATE_WORKING;
	memcpy(&C->addr, addr, addrlen);
	C->addrlen = addrlen;

	/* Get name information in user-friendly form: */
	rv = getnameinfo(addr, addrlen, host, sizeof(host), port, sizeof(port), 0);
	if (rv != 0) {
		rv = getnameinfo(addr, addrlen, host, sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
	}
	if (rv != 0) {
		snprintf(C->name, sizeof(C->name), "C-%d", rand() % 1000000);
		trfb_msg("Can not determine client information. It will be %s", C->name);
	} else {
		snprintf(C->name, sizeof(C->name), "%s:%s", host, port);
	}

	trfb_msg("I:starting operationing with client [%s]", C->name);

	/* Run connection processing thread: */
	if (thrd_create(&C->thread, connection, C) != thrd_success) {
		free(C);
		trfb_msg("Can't start thread");
		return NULL;
	}

	return C;
}

#ifndef BUFSIZ
# define BUFSIZ 8192
#endif

static int negotiate(trfb_connection_t* con)
{
	trfb_msg_protocol_version_t msg;
	unsigned char buf[256];
	size_t len;

	msg.proto = trfb_v8;

	len = sizeof(buf);
	if (trfb_msg_protocol_version_encode(&msg, buf, &len)) {
		trfb_msg("Can't encode ProtocolVersion message");
		return -1;
	}

	trfb_connection_write_all(con, buf, len);
	trfb_connection_read_all(con, buf, 12);
	buf[12] = 0;
	trfb_msg("I:[%s] client wants version: %s", con->name, buf);

	if (trfb_msg_protocol_version_decode(&msg, buf, 12)) {
		trfb_msg("[%s] Can't decode ProtocolVersion message from client", con->name);
		return -1;
	}

	con->version = msg.proto;

	/* TODO: security */
	if (con->version < trfb_v7) {
		buf[0] = 0;
		buf[1] = 0;
		buf[2] = 0;
		buf[3] = 1;
		trfb_connection_write_all(con, buf, 4);
	} else {
		buf[0] = 1;
		buf[1] = 1; /* NONE */
		trfb_connection_write_all(con, buf, 2);

		trfb_connection_read_all(con, buf, 1);
		trfb_msg("I:[%s] client security type %d", con->name, buf[0]);
		if (buf[0] != 1) {
			trfb_msg("[%s] Client has sent invalid security type", con->name);
			return -1;
		}
	}

	if (con->version >= trfb_v8) {
		memset(buf, 0, 4); /* Security Ok */
		trfb_connection_write_all(con, buf, 4);
		trfb_msg("I:[%s] sent security Ok", con->name);
	}

	trfb_connection_read_all(con, buf, 1);
	trfb_msg("I:ClientInit: %02X", buf[0]);

	/* Sending ServerInit: */
	buf[0] = con->server->fb->width / 256;
	buf[1] = con->server->fb->width % 256;
	buf[2] = con->server->fb->height / 256;
	buf[3] = con->server->fb->height % 256;
	buf[4] = 32; /* bits per pixel */
	buf[5] = 24; /* depth */
	buf[6] = 0; /* big-endian */
	buf[7] = 1; /* true-color */
	buf[8] = 0x00; /* red-max */
	buf[9] = 0xff;
	buf[10] = 0x00; /* green-max */
	buf[11] = 0xff;
	buf[12] = 0x00; /* blue-max */
	buf[13] = 0xff;
	buf[14] = 16; /* red-shift */
	buf[15] = 8;  /* green-shift */
	buf[16] = 0;  /* blue-shift */
	buf[17] = buf[18] = buf[19] = 0; /* padding */
	buf[20] = 0;
	buf[21] = 0;
	buf[22] = 0;
	buf[23] = 4; /* name length */
	buf[24] = 'T'; buf[25] = 'E'; buf[26] = 'S'; buf[27] = 'T';
	trfb_connection_write_all(con, buf, 28);

	trfb_msg("I:Sent framebuffer information to client");

	return 0;
}

#if 0
static void print_bin(const char *name, unsigned char *p, size_t l)
{
	size_t i;

	printf("%s[%u] = {", name, (unsigned)l);
	for (i = 0; i < l; i++) {
		if (i % 16 == 0) {
			printf("\n\t");
		}
		printf("%02x ", p[i]);
	}
	printf("\n}\n");
}
#endif

static void check_stopped(trfb_connection_t *con)
{
	mtx_lock(&con->lock);
	if (con->state == TRFB_STATE_STOP) {
		trfb_msg("I:Connection stopped");
		con->state = TRFB_STATE_STOPPED;
		mtx_unlock(&con->lock);
		thrd_exit(0);
	}
	mtx_unlock(&con->lock);
}

static void SetEncodings(trfb_connection_t *con);
static void SetPixelFormat(trfb_connection_t *con);
static void UpdateRequest(trfb_connection_t *con);
static void KeyEvent(trfb_connection_t *con);
static void PointerEvent(trfb_connection_t *con);
static void ClientCutText(trfb_connection_t *con);

static struct msg_types {
	unsigned char type;
	void (*process)(trfb_connection_t *con);
} msg_types[] = {
	{ 0, SetPixelFormat },
	{ 2, SetEncodings },
	{ 3, UpdateRequest },
	{ 4, KeyEvent },
	{ 5, PointerEvent },
	{ 6, ClientCutText },
	{ 0, NULL }
};

static int connection(void *con_in)
{
	trfb_connection_t *con = con_in;
	ssize_t l;
	int i;
	unsigned char type;

#define EXIT_THREAD(s) \
	do { \
		mtx_lock(&con->lock); \
		con->state = s; \
		mtx_unlock(&con->lock); \
		thrd_exit(0); \
	} while (0)

	mtx_lock(&con->lock);
	con->state = TRFB_STATE_WORKING;
	mtx_unlock(&con->lock);

	if (negotiate(con)) {
		EXIT_THREAD(TRFB_STATE_ERROR);
	}
	trfb_msg("I:negotiation done");

	for (;;) {
		check_stopped(con);

		l = trfb_connection_read(con, &type, 1);
		if (l < 0) {
			EXIT_THREAD(TRFB_STATE_ERROR);
		}

		if (l == 1) {
#if EXTRA_DEBUG
			trfb_msg("I:message[%d]", type);
#endif

			for (i = 0; msg_types[i].process; i++)
				if (msg_types[i].type == type)
					break;

			if (!msg_types[i].process) {
				trfb_msg("Message of unknown type: %d\n", type);
				EXIT_THREAD(TRFB_STATE_ERROR);
			}

			msg_types[i].process(con);
		}
	}

	return 0;
}

ssize_t trfb_connection_read(trfb_connection_t *con, void *buf, ssize_t len)
{
	ssize_t l;

	for (;;) {
		l = trfb_io_read(con->io, buf, len, 1000);

		if (l > 0) {
			return l;
		} else if (l < 0) {
			return -1;
		}

		check_stopped(con);
	}

	return -1; /* not reached */
}

void trfb_connection_read_all(trfb_connection_t *con, void *buf, ssize_t len)
{
	unsigned char *p = buf;
	ssize_t l;
	ssize_t pos = 0;

	while (pos < len) {
		l = trfb_connection_read(con, p + pos, len - pos);

		if (l < 0) {
			trfb_msg("Can't read from client!");
			EXIT_THREAD(TRFB_STATE_ERROR);
		}

		pos += l;
	}

	return;
}

ssize_t trfb_connection_write(trfb_connection_t *con, const void *buf, ssize_t len)
{
	ssize_t l;

	for (;;) {
		l = trfb_io_write(con->io, buf, len, 1000);

		if (l > 0) {
			return l;
		} else if (l < 0) {
			return -1;
		}

		check_stopped(con);
	}

	return -1; /* not reached */
}

void trfb_connection_flush(trfb_connection_t *con)
{
	ssize_t r;

	for (;;) {
		r = trfb_io_flush(con->io, 1000);
		if (r < 0) {
			EXIT_THREAD(TRFB_STATE_ERROR);
		}

		if (r == 0) {
			return;
		}

		check_stopped(con);
	}
}

void trfb_connection_write_all(trfb_connection_t *con, const void *buf, ssize_t len)
{
	const unsigned char *p = buf;
	ssize_t l;
	ssize_t pos = 0;

	while (pos < len) {
		l = trfb_connection_write(con, p + pos, len - pos);

		if (l < 0) {
			EXIT_THREAD(TRFB_STATE_ERROR);
		}

		pos += l;
	}

	trfb_connection_flush(con);

	return;
}

uint16_t trfb_connection_read_u16(trfb_connection_t *con)
{
	unsigned char buf[2];
	trfb_connection_read_all(con, buf, 2);
	return buf[0] * 256 + buf[1];
}

uint16_t trfb_connection_read_u32(trfb_connection_t *con)
{
	unsigned char buf[4];
	trfb_connection_read_all(con, buf, 4);
	return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

static void SetEncodings(trfb_connection_t *con)
{
	unsigned cnt;
	unsigned i;
	uint32_t enc;
	unsigned char buf[4];

	trfb_connection_read_all(con, buf, 3);
	cnt = buf[1] * 256 + buf[2];

	trfb_msg("I:client supports %d encodings...", (int)cnt);

	for (i = 0; i < cnt; i++) {
		enc = trfb_connection_read_u32(con);
		trfb_msg("I:Supported encoding: %08x", (int)enc);
	}
}

static void SetPixelFormat(trfb_connection_t *con)
{
	unsigned char buf[20];

	trfb_connection_read_all(con, buf + 1, 19);
	con->format.bpp = buf[4];
	con->format.depth = buf[5];
	con->format.big_endian = buf[6];
	con->format.true_color = buf[7];
	con->format.rmax = buf[8] * 256 + buf[9];
	con->format.gmax = buf[10] * 256 + buf[11];
	con->format.bmax = buf[12] * 256 + buf[13];
	con->format.rshift = buf[14];
	con->format.gshift = buf[15];
	con->format.bshift = buf[16];

	trfb_msg("I:[%s] FORMAT: bpp = %d, depth = %d, big_endian = %d, true_color = %d", con->name,
			con->format.bpp,
			con->format.depth,
			con->format.big_endian,
			con->format.true_color);

	if (con->fb)
		trfb_framebuffer_free(con->fb);
	trfb_server_lock_fb(con->server, 0);
	con->fb = trfb_framebuffer_create_of_format(con->server->fb->width, con->server->fb->width, &con->format);
	trfb_server_unlock_fb(con->server);

	if (!con->fb) {
		trfb_msg("Can not create framebuffer for requested format.");
		EXIT_THREAD(TRFB_STATE_ERROR);
	}
}

static void UpdateRequest(trfb_connection_t *con)
{
	unsigned char buf[256];
	unsigned char incr;
	unsigned xpos, ypos, width, height;

	trfb_connection_read_all(con, buf, 9);
	incr = buf[0];
	(void)incr; /* used :) */
	xpos = buf[1] * 256 + buf[2];
	ypos = buf[3] * 256 + buf[4];
	width = buf[5] * 256 + buf[6];
	height = buf[7] * 256 + buf[8];

#if EXTRA_DEBUG
	trfb_msg("I:client requested update: (%d, %d) - (%d, %d)", (int)xpos, (int)ypos, (int)width, (int)height);
#endif

	if (!con->fb) {
		trfb_server_lock_fb(con->server, 0);
		con->fb = trfb_framebuffer_copy(con->server->fb);
		trfb_server_unlock_fb(con->server);

		if (!con->fb) {
			trfb_msg("Can not copy server framebuffer");
			EXIT_THREAD(TRFB_STATE_ERROR);
		}
	}

	trfb_server_lock_fb(con->server, 0);
	if (trfb_framebuffer_convert(con->fb, con->server->fb)) {
		trfb_server_unlock_fb(con->server);
		trfb_msg("Can not convert server framebuffer");
		EXIT_THREAD(TRFB_STATE_ERROR);
	}
	trfb_server_unlock_fb(con->server);

	if (width > con->fb->width) {
		width = con->fb->width;
	}

	if (height > con->fb->height) {
		height = con->fb->height;
	}

	if (xpos > width || ypos > height) {
		trfb_msg("I:Client wants rect out of range. Ignoring...");
		return;
	}

	trfb_framebuffer_endian(con->fb, con->format.big_endian);

	/* TODO: send response */
	buf[0] = 0; /* message type */
	buf[1] = 0; /* pad */
	buf[2] = 0;
	buf[3] = 1; /* 1 rect */
	buf[4] = xpos / 256;
	buf[5] = xpos % 256; /* x-position */
	buf[6] = ypos / 256;
	buf[7] = ypos % 256; /* y-position */
	buf[8] = width / 256;
	buf[9] = width % 256;
	buf[10] = height / 256;
	buf[11] = height % 256;
	buf[12] = 0;
	buf[13] = 0;
	buf[14] = 0;
	buf[15] = 0;
	trfb_connection_write_all(con, buf, 16);

	trfb_connection_write_all(con, con->fb->pixels, con->fb->width * con->fb->height * con->fb->bpp);
}

static void KeyEvent(trfb_connection_t *con)
{
	unsigned char buf[8];
	unsigned char down;
	uint32_t code;
	trfb_event_t event;

	trfb_connection_read_all(con, buf, 7);
	down = buf[0];
	code = (buf[3] << 24) | (buf[4] << 16) | (buf[5] << 8) | buf[6];

	event.type = TRFB_EVENT_KEY;
	event.event.key.down = down;
	event.event.key.code = code;

	trfb_server_add_event(con->server, &event);
}

static void PointerEvent(trfb_connection_t *con)
{
	unsigned char buf[5];
	trfb_event_t event;

	trfb_connection_read_all(con, buf, 5);

	event.event.pointer.button = buf[0];
	event.event.pointer.x = buf[1] * 256 + buf[2];
	event.event.pointer.y = buf[3] * 256 + buf[4];
	event.type = TRFB_EVENT_POINTER;

	trfb_server_add_event(con->server, &event);
}

static void ClientCutText(trfb_connection_t *con)
{
	unsigned char buf[7];
	trfb_event_t event;

	trfb_connection_read_all(con, buf, 7);
	event.type = TRFB_EVENT_CUT_TEXT;
	event.event.cut_text.len = (buf[3] << 24) | (buf[4] << 16) | (buf[5] << 8) | buf[6];
	event.event.cut_text.text = malloc(event.event.cut_text.len + 1);
	if (!event.event.cut_text.text) {
		trfb_msg("Not enought memory");
		EXIT_THREAD(TRFB_STATE_ERROR);
	}

	/* WARNING: this could cause memory leak. This is very so bad and we need to find an solution. */
	trfb_connection_read_all(con, event.event.cut_text.text, event.event.cut_text.len);

	trfb_server_add_event(con->server, &event);
}

