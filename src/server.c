#include <trfb.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

static int server(void *srv_in);

static void xsleep(unsigned ms)
{
	usleep(ms * 1000);
}

int trfb_server_start(trfb_server_t *srv)
{
	mtx_lock(&srv->lock);
	if (srv->clients || srv->state != TRFB_STATE_STOPPED) {
		mtx_unlock(&srv->lock);
		trfb_msg("E: server is always started but trfb_server_start called!");
		return -1;
	}
	mtx_unlock(&srv->lock);

	if (!srv->fb || srv->sock < 0) {
		trfb_msg("Server parameters is not set. Invalid trfb_server content.");
		return -1;
	}

	if (thrd_create(&srv->thread, server, srv) != thrd_success) {
		trfb_msg("Can't start thread");
		return -1;
	}

	for (;;) {
		mtx_lock(&srv->lock);
		if (srv->state == TRFB_STATE_WORKING) {
			mtx_unlock(&srv->lock);
			trfb_msg("I:server started!");
			return 0;
		}

		if (srv->state != TRFB_STATE_STOPPED) {
			mtx_unlock(&srv->lock);
			trfb_msg("E:invalid server state");
			return -1;
		}

		mtx_unlock(&srv->lock);

		xsleep(1);
	}

	return 0; /* not reached */
}

static void stop_all_connections(trfb_server_t *srv);
static int server(void *srv_in)
{
	trfb_server_t *srv = srv_in;
	trfb_connection_t *con;
	trfb_connection_t *prev;
	trfb_connection_t *f;
	fd_set fds;
	struct timeval tv;
	int rv;
	struct sockaddr addr;
	socklen_t addrlen;
	int sock;

#define EXIT_THREAD(s) \
	do { \
		stop_all_connections(srv); \
		mtx_lock(&srv->lock); \
		srv->state = s; \
		mtx_unlock(&srv->lock); \
		thrd_exit(0); \
		return 0; \
	} while (0)

	trfb_msg("I:starting server...");
	mtx_lock(&srv->lock);
	srv->state = TRFB_STATE_WORKING;
	mtx_unlock(&srv->lock);

	if (listen(srv->sock, 8)) {
		trfb_msg("listen failed");
		EXIT_THREAD(TRFB_STATE_ERROR);
	}

	for (;;) {
		mtx_lock(&srv->lock);
		if (srv->state == TRFB_STATE_STOP) {
			mtx_unlock(&srv->lock);
			trfb_msg("I:server stopped");
			EXIT_THREAD(TRFB_STATE_STOPPED);
		}
		mtx_unlock(&srv->lock);

		FD_ZERO(&fds);
		FD_SET(srv->sock, &fds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		rv = select(srv->sock + 1, &fds, NULL, NULL, &tv);
		if (rv < 0) {
			trfb_msg("select failed");
			EXIT_THREAD(TRFB_STATE_ERROR);
		}

		if (rv > 0) {
			sock = accept(srv->sock, &addr, &addrlen);
			trfb_msg("I:new client!");
			if (sock >= 0) {
				con = trfb_connection_create(srv, sock, &addr, addrlen);
				if (!con) {
					trfb_msg("W:can not create new connection");
				} else {
					mtx_lock(&srv->lock);
					con->next = srv->clients;
					srv->clients = con;
					mtx_unlock(&srv->lock);
				}
			}
		}

		prev = NULL;
		f = NULL;
		for (con = srv->clients; con;) {
			mtx_lock(&con->lock);
			if (con->state != TRFB_STATE_WORKING) {
				mtx_unlock(&con->lock);
				thrd_join(con->thread, &rv);

				if (prev) {
					prev->next = con->next;
					f = con;
					con = con->next;
				} else {
					srv->clients = con->next;
					f = con;
					con = con->next;
				}

				if (f) {
					trfb_connection_free(f);
					f = NULL;
				}
			} else {
				mtx_unlock(&con->lock);
				con = con->next;
			}
		}
	}

	return 0;
}

int trfb_server_stop(trfb_server_t *srv)
{
	int res = 0;

	mtx_lock(&srv->lock);
	srv->state = TRFB_STATE_STOP;
	mtx_unlock(&srv->lock);

	while (trfb_server_get_state(srv) == TRFB_STATE_STOP) {
		xsleep(2);
	}

	thrd_join(srv->thread, &res);

	return 0;
}

static void stop_all_connections(trfb_server_t *srv)
{
	trfb_connection_t *con;
	trfb_connection_t *connections;
	int work, res;

	trfb_msg("I:waiting all clients to stop...");

	mtx_lock(&srv->lock);
	connections = srv->clients;
	srv->clients = NULL;
	mtx_unlock(&srv->lock);

	for (con = connections; con; con = con->next) {
		mtx_lock(&con->lock);
		if (con->state == TRFB_STATE_WORKING)
			con->state = TRFB_STATE_STOP;
		mtx_unlock(&con->lock);
	}

	while (connections) {
		work = 0;
		for (con = connections; con; con = con->next) {
			mtx_lock(&con->lock);
			if (con->state == TRFB_STATE_STOP) {
				++work;
			}
			mtx_unlock(&con->lock);
		}

		if (work) {
			xsleep(1);
		} else {
			for (con = connections; con; con = con->next) {
				thrd_join(con->thread, &res);
			}

			while (connections) {
				con = connections;
				connections = con->next;
				trfb_connection_free(con);
			}
		}
	}

	trfb_msg("I:all clients have been stoped...");
	return;
}

int trfb_server_lock_fb(trfb_server_t *srv, int w)
{
	if (!srv || !srv->fb)
		return -1;
	mtx_lock(&srv->fb->lock);
	if (w) {
		srv->updated = 0;
	} else {
		srv->updated++;
	}

	return 0;
}

int trfb_server_unlock_fb(trfb_server_t *srv)
{
	if (!srv || !srv->fb)
		return -1;
	mtx_unlock(&srv->fb->lock);

	return 0;
}

int trfb_server_add_event(trfb_server_t *srv, trfb_event_t *event)
{
	if (!srv || !event) {
		return -1;
	}

	mtx_lock(&srv->lock);
	if (srv->event_len >= TRFB_EVENTS_QUEUE_LEN) {
		mtx_unlock(&srv->lock);
		return -1;
	}

	if (trfb_event_move(srv->events + ((srv->event_cur + srv->event_len) % TRFB_EVENTS_QUEUE_LEN), event)) {
		mtx_unlock(&srv->lock);
		return -1;
	}

	srv->event_len++;
	mtx_unlock(&srv->lock);

	return 0;
}

int trfb_server_poll_event(trfb_server_t *srv, trfb_event_t *event)
{
	if (!srv || !event) {
		return 0;
	}

	mtx_lock(&srv->lock);
	if (srv->event_len == 0) {
		mtx_unlock(&srv->lock);
		return 0;
	}

	if (trfb_event_move(event, srv->events + srv->event_cur)) {
		mtx_unlock(&srv->lock);
		return 0;
	}

	srv->event_cur = (srv->event_cur + 1) % TRFB_EVENTS_QUEUE_LEN;
	srv->event_len--;

	mtx_unlock(&srv->lock);

	return 1;
}

void trfb_event_clear(trfb_event_t *event)
{
	if (!event)
		return;
	if (event->type == TRFB_EVENT_CUT_TEXT)
		free(event->event.cut_text.text);
	memset(event, 0, sizeof(trfb_event_t));
}

int trfb_event_move(trfb_event_t *dst, trfb_event_t *src)
{
	if (!dst || !src)
		return -1;

	memcpy(dst, src, sizeof(trfb_event_t));
	memset(src, 0, sizeof(trfb_event_t));

	return 0;
}

unsigned trfb_server_updated(trfb_server_t *srv)
{
	unsigned r;

	if (!srv) {
		trfb_msg("Invalid argument!");
		return 0;
	}

	mtx_lock(&srv->fb->lock);
	r = srv->updated;
	mtx_unlock(&srv->fb->lock);

	return r;
}

