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
	close(con->sock);
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

	C->pixels = calloc(srv->width * srv->height * 3, 1);
	if (!C->pixels) {
		free(C);
		trfb_msg("Not enought memory for pixels buffer");
		return NULL;
	}

	C->sock = sock;
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

	msg.proto = trfb_v8;

	if (trfb_msg_protocol_version_send(&msg, con->sock)) {
		trfb_msg("Can't send ProtocolVersion message");
		return -1;
	}

	if (trfb_msg_protocol_version_recv(&msg, con->sock)) {
		trfb_msg("[%s] Can't get ProtocolVersion message from client", con->name);
		return -1;
	}

	con->version = msg.proto;

	/* TODO: security */
	buf[0] = 1;
	buf[1] = 1; /* NONE */
	if (trfb_send_all(con->sock, buf, 2) != 2) {
		return -1;
	}

	if (trfb_recv_all(con->sock, buf, 1) != 1) {
		return -1;
	}

	if (buf[0] != 1) {
		trfb_msg("[%s] Client has sent invalid security type", con->name);
		return -1;
	}

	memset(buf, 0, 4); /* Security Ok */
	if (trfb_send_all(con->sock, buf, 4) != 4) {
		return -1;
	}

	return 0;
}

static int connection(void *con_in)
{
	trfb_connection_t *con = con_in;
	unsigned char buf[BUFSIZ];
	ssize_t len, l;
	ssize_t pos = -1;
	fd_set fds;
	int rv;
	struct timeval tv;
#define EXIT_THREAD(s) \
	do { \
		mtx_lock(&con->lock); \
		con->state = s; \
		mtx_unlock(&con->lock); \
		thrd_exit(0); \
		return 0; \
	} while (0)

	mtx_lock(&con->lock);
	con->state = TRFB_STATE_WORKING;
	mtx_unlock(&con->lock);

	if (negotiate(con)) {
		EXIT_THREAD(TRFB_STATE_ERROR);
	}

	for (;;) {
		mtx_lock(&con->lock);
		if (con->state == TRFB_STATE_STOP) {
			trfb_msg("I:Connection stopped");
			mtx_unlock(&con->lock);
			EXIT_THREAD(TRFB_STATE_STOPPED);
		}
		mtx_unlock(&con->lock);

		FD_ZERO(&fds);
		FD_SET(con->sock, &fds);
		tv.tv_sec = 1;
		tv.tv_usec = 1;

		if (pos < 0) {
			rv = select(con->sock + 1, &fds, NULL, NULL, &tv);
			if (rv < 0) {
				trfb_msg("select failed");
				EXIT_THREAD(TRFB_STATE_ERROR);
			}

			if (rv > 0) {
				len = recv(con->sock, buf, sizeof(buf), 0);
				if (len <= 0) {
					trfb_msg("Can't read data from socket");
					EXIT_THREAD(TRFB_STATE_ERROR);
				}

				pos = 0;
			}
		} else {
			rv = select(con->sock + 1, NULL, &fds, NULL, &tv);
			if (rv < 0) {
				trfb_msg("select failed");
				EXIT_THREAD(TRFB_STATE_ERROR);
			}

			if (rv > 0) {
				l = send(con->sock, buf + pos, len - pos, 0);
				if (l <= 0) {
					trfb_msg("Can't write data into socket");
					EXIT_THREAD(TRFB_STATE_ERROR);
				}

				pos += l;

				if (pos == len) { /* It is the end */
					pos = -1;
				}
			}
		}
	}

	return 0;
}

