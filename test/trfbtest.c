#include <trfb.h>
#include <c11threads.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static int quit_now = 0;
static void sigint(int sig)
{
	quit_now = 1;
}

int main(int argc, char *argv[])
{
	trfb_server_t *srv;
	unsigned i, j, di = 0;
	trfb_event_t event;

	signal(SIGINT, sigint);

	srv = trfb_server_create(640, 480, 4);
	if (!srv) {
		fprintf(stderr, "Error: can't create server!\n");
		return 1;
	}

	if (trfb_server_bind(srv, "localhost", "5913")) {
		fprintf(stderr, "Error: can't bind!\n");
		return 1;
	}

	if (trfb_server_start(srv)) {
		fprintf(stderr, "Error: can't start server!\n");
		return 1;
	}

	for (;;) {
		trfb_server_lock_fb(srv, 1);
		for (i = 0; i < 256; i++) {
			for (j = 0; j < 256; j++) {
				trfb_framebuffer_set_pixel(srv->fb, (i + di) % 256, j, TRFB_RGB(i, j, 100));
			}
		}
		trfb_server_unlock_fb(srv);
		// di = (di + 10) % 256;

		while (trfb_server_poll_event(srv, &event)) {
			if (event.type == TRFB_EVENT_KEY && (
						event.event.key.code == 'q' ||
						event.event.key.code == 'Q' ||
						event.event.key.code == 0xff1b)) {
				quit_now = 1;
			}
			printf("EVENT: %d\n", event.type);
			trfb_event_clear(&event);
		}

		if (quit_now) {
			trfb_server_stop(srv);
			trfb_server_destroy(srv);
			exit(0);
		}

		usleep(1000);
	}

	return 0;
}
