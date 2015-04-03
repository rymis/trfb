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
	webcam_color_t *data = cam->image;

	trfb_server_lock_fb(srv, 1);

	for (y = 0; y < cam->height; y++) {
		for (x = 0; x < cam->width; x++) {
			trfb_framebuffer_set_pixel(srv->fb, x, y, data[y * cam->width + x]);
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
	int brightness;
	int contrast;
	int saturation;
	int gamma;

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

	brightness = webcam_get_control(cam, WEBCAM_BRIGHTNESS);
	saturation = webcam_get_control(cam, WEBCAM_SATURATION);
	gamma = webcam_get_control(cam, WEBCAM_GAMMA);
	contrast = webcam_get_control(cam, WEBCAM_CONTRAST);
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
			} else if (event.type == TRFB_EVENT_KEY && event.event.key.down) {
#define INCR(param, nm) \
				do { \
					if (param >= 0) { \
						++param; \
						if (param > 100) param = 100; \
						webcam_set_control(cam, nm, param); \
						printf("INFO: %s = %d\n", #param, param); \
					} else { \
						printf("Parameter %s is not supported\n", #param); \
					} \
				} while (0)

#define DECR(param, nm) \
				do { \
					if (param >= 0) { \
						--param; \
						if (param < 0) param = 0; \
						webcam_set_control(cam, nm, param); \
						printf("INFO: %s = %d\n", #param, param); \
					} else { \
						printf("Parameter %s is not supported\n", #param); \
					} \
				} while (0)

				if (event.event.key.code == 'a' || event.event.key.code == 'A') {
					INCR(brightness, WEBCAM_BRIGHTNESS);
				} else if (event.event.key.code == 'z' || event.event.key.code == 'Z') {
					DECR(brightness, WEBCAM_BRIGHTNESS);
				} else if (event.event.key.code == 's' || event.event.key.code == 'S') {
					INCR(contrast, WEBCAM_CONTRAST);
				} else if (event.event.key.code == 'x' || event.event.key.code == 'X') {
					DECR(contrast, WEBCAM_CONTRAST);
				} else if (event.event.key.code == 'd' || event.event.key.code == 'D') {
					INCR(gamma, WEBCAM_GAMMA);
				} else if (event.event.key.code == 'c' || event.event.key.code == 'C') {
					DECR(gamma, WEBCAM_GAMMA);
				} else if (event.event.key.code == 'f' || event.event.key.code == 'F') {
					INCR(saturation, WEBCAM_SATURATION);
				} else if (event.event.key.code == 'v' || event.event.key.code == 'V') {
					DECR(saturation, WEBCAM_SATURATION);
				}

			}
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

