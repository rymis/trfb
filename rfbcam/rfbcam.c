#include <trfb.h>
#include <signal.h>
#include <string.h>
#include <libwebcam.h>
#include <stdio.h>
#include <unistd.h>

static int quit_now = 0;
static void sigint(int sig)
{
	quit_now = 1;
}

static void draw_image(webcam_t *cam, trfb_server_t *srv)
{
	unsigned x, y;
	unsigned W = cam->width * 3;
	unsigned off;
	unsigned char *data = cam->img;

	trfb_server_lock_fb(srv, 1);

	for (y = 0; y < cam->height; y++) {
		for (x = 0; x < cam->width; x++) {
			off = W * y + x;
			trfb_framebuffer_set_pixel(srv->fb, x, y,
					TRFB_RGB(data[off], data[off + 1], data[off + 2]));
		}
	}

	trfb_server_unlock_fb(srv);
}

int main(int argc, char *argv[])
{
	trfb_server_t *srv;
	webcam_t *cam;
	const unsigned width = 640;
	const unsigned height = 480;
	trfb_event_t event;

	signal(SIGINT, sigint);

	cam = webcam_open(0, 640, 480);
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

	webcam_start(cam);
	for (;;) {
		if (trfb_server_updated(srv)) {
			if (webcam_wait_frame(cam, 10) > 0) {
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
	webcam_stop(cam);
	webcam_close(cam);
	trfb_server_stop(srv);
	trfb_server_destroy(srv);

	return 0;
}

