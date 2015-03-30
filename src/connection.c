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
	free(con->pixels);
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
		trfb_msg("Fatal error: addrlen > sizeof(struct sockaddr)");
		free(C);
		return NULL;
	}

	C->io = trfb_io_socket_wrap(sock);
	if (!C->io) {
		free(C);
		trfb_msg("Can not wrap socket");
		return NULL;
	}

	C->pixels = calloc(srv->width * srv->height, sizeof(uint32_t));
	if (!C->pixels) {
		free(C);
		trfb_msg("Not enought memory for pixels buffer");
		return NULL;
	}

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
		free(C->pixels);
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

	if (trfb_msg_protocol_version_decode(&msg, buf, 12)) {
		trfb_msg("[%s] Can't decode ProtocolVersion message from client", con->name);
		return -1;
	}

	con->version = msg.proto;

	/* TODO: security */
	buf[0] = 1;
	buf[1] = 1; /* NONE */
	trfb_connection_write_all(con, buf, 2);

	trfb_connection_read_all(con, buf, 1);
	if (buf[0] != 1) {
		trfb_msg("[%s] Client has sent invalid security type", con->name);
		return -1;
	}

	memset(buf, 0, 4); /* Security Ok */
	trfb_connection_write_all(con, buf, 4);
	trfb_connection_read_all(con, buf, 1);

	trfb_msg("I:ClientInit: %02X", buf[0]);

	/* Sending ServerInit: */
	buf[0] = con->server->width / 256;
	buf[1] = con->server->width % 256;
	buf[2] = con->server->height / 256;
	buf[3] = con->server->height % 256;
	buf[4] = 32; /* bits per pixel */
	buf[5] = 3; /* depth */
	buf[6] = 1; /* big-endian */
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

static int connection(void *con_in)
{
	trfb_connection_t *con = con_in;
	unsigned char buf[BUFSIZ];
	ssize_t len, l;
	ssize_t pos = -1;
	int rv;
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

		trfb_connection_read_all(con, &type, 1);

		switch (type) {
			case 0: /* SetPixelFormat */
				break;
			case 2: /* Set encoding */
				SetEncodings(con);
				break;
			case 3: /* FrameBuffer update request */
				break;
			case 4: /* Key Event */
				break;
			case 5: /* PointerEvent */
				break;
			case 6: /* ClientCutText */
				break;
			default:
				trfb_msg("Message of unknown type: %d\n", type);
				EXIT_THREAD(TRFB_STATE_ERROR);
				break;
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
	return (buf[0] << 8) + buf[1];
}

uint16_t trfb_connection_read_u32(trfb_connection_t *con)
{
	unsigned char buf[4];
	trfb_connection_read_all(con, buf, 4);
	return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

static void SetEncodings(trfb_connection_t *con)
{
	uint16_t cnt;
	unsigned i;
	uint32_t enc;

	trfb_connection_read_all(con, &cnt, 1); /* pad */
	cnt = trfb_connection_read_u16(con);

	for (i = 0; i < cnt; i++) {
		enc = trfb_connection_read_u32(con);
		trfb_msg("I:Supported encoding: %08x", (int)enc);
	}
}

