#include <trfb.h>
#include <signal.h>
#include <string.h>

#include "lwebcam/lwebcam.h"

static int quit_now = 0;
static void sigint(int sig)
{
	quit_now = 1;
}

static void draw_image(struct webcam *cam, trfb_server_t *srv)
{
	unsigned x, y;
	unsigned W = cam->width * 3;
	unsigned sW;
	unsigned off;
	uint32_t *pixels;

	trfb_server_lock_fb(srv, 1);
	pixels = srv->fb->pixels;
	sW = srv->fb->width;

	for (y = 0; y < cam->height; y++) {
		for (x = 0; x < cam->width; x++) {
			off = W * y + x;
			pixels[y * sW + x] = TRFB_RGB(
						cam->img_data[off],
						cam->img_data[off + 1],
						cam->img_data[off + 2]
						);
		}
	}

	trfb_server_unlock_fb(srv);
}

int main(int argc, char *argv[])
{
	trfb_server_t *srv;
	struct webcam *cam;
	const unsigned width = 640;
	const unsigned height = 480;
	trfb_event_t event;

	signal(SIGINT, sigint);

	cam = webcam_open("/dev/video0", WEBCAM_IO_METHOD_READ, width, height);
	if (!cam) {
		fprintf(stderr, "Error: can't open webcam!\n");
		return 1;
	}

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

	webcam_start_capturing(cam);
	for (;;) {
		if (trfb_server_updated(srv)) {
			if (webcam_wait_frame(cam, 1) > 0) {
				draw_image(cam, srv);
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
	webcam_stop_capturing(cam);
	webcam_close(cam);
	trfb_server_stop(srv);
	trfb_server_destroy(srv);

	return 0;
}

