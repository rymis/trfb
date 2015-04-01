#include "webcam/webcam.h"
#include <trfb.h>

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

	if (trfb_server_bind(srv, "localhost", "5913")) {
		fprintf(stderr, "Error: can't bind!\n");
		return 1;
	}

	if (trfb_server_start(srv)) {
		fprintf(stderr, "Error: can't start server!\n");
		return 1;
	}

	webcam_stream(cam, true);
	for (;;) {
		webcam_grab(cam, &frame);

		if (frame.length > 0) {
			draw_image(cam, &frame, srv);
		}

		/* TODO: wait for event */
		/* TODO: wait for update */
	}
	webcam_stream(cam, false);
	trfb_server_stop(srv);
	trfb_server_free(srv);

	return 0;
}

