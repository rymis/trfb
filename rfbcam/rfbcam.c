#include "webcam/webcam.h"
#include <trfb.h>
#include <signal.h>

static int quit_now = 0;
static void sigint(int sig)
{
	quit_now = 1;
}

static void draw_image(webcam_t *cam, buffer_t *frame, trfb_server_t *srv)
{
	trfb_server_lock_fb(srv, 1);

	if (srv->fb->width * srv->fb->height * srv->fb->bpp >= frame->length)
		memcpy(srv->fb->pixels, frame->start, frame->length);

	trfb_server_unlock_fb(srv);
}

int main(int argc, char *argv[])
{
	trfb_server_t *srv;
	webcam_t *cam;
	buffer_t frame = { NULL, 0 };
	const unsigned width = 640;
	const unsigned height = 480;
	trfb_event_t event;

	signal(SIGINT, sigint);

	cam = webcam_open("/dev/video0");
	if (!cam) {
		fprintf(stderr, "Error: can't open webcam!\n");
		return 1;
	}
	webcam_resize(cam, width, height);

	srv = trfb_server_create(width, height, 4);
	if (!srv) {
		fprintf(stderr, "Error: can't create server!\n");
		return 1;
	}

	if (trfb_server_bind(srv, argc > 1? argv[1]: "localhost", "5913")) {
		fprintf(stderr, "Error: can't bind!\n");
		return 1;
	}

	if (trfb_server_start(srv)) {
		fprintf(stderr, "Error: can't start server!\n");
		return 1;
	}

	webcam_stream(cam, true);
	for (;;) {
		if (trfb_server_updated(srv)) {
			webcam_grab(cam, &frame);

			if (frame.length > 0) {
				draw_image(cam, &frame, srv);
			}
		}

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
		/* TODO: wait for update */
	}
	webcam_stream(cam, false);
	trfb_server_stop(srv);
	trfb_server_destroy(srv);

	return 0;
}

