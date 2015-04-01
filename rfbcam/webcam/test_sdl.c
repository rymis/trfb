/* test_sdl.c */
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/time.h>

#include "webcam.h"

#include <SDL.h>

static int lineY = 100;

/* Create SDL_Surface from image: */
SDL_Surface* img2surface(unsigned width, unsigned height, buffer_t* img)
{
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
	const Uint32 Rmask = 0x000000FF;
	const Uint32 Gmask = 0x0000FF00;
	const Uint32 Bmask = 0x00FF0000;
#else
	const Uint32 Rmask = 0x00FF0000;
	const Uint32 Gmask = 0x0000FF00;
	const Uint32 Bmask = 0x000000FF;
#endif

	if (img->length < width * height) {
		fprintf(stderr, "Error: length invalid!\n");
		return NULL;
	}

	return SDL_CreateRGBSurfaceFrom(img->start, width, height, 24, width * 3,
			Rmask, Gmask, Bmask, 0);
}

static void draw_image(struct webcam *cam, buffer_t *frame, SDL_Surface *screen)
{
	SDL_Surface *bitmap;
	SDL_Rect dest;

	if (SDL_MUSTLOCK(screen)) {
		if (SDL_LockSurface(screen) < 0)
			return;
	}

	bitmap = img2surface(cam->width, cam->height, frame);
	/* Draw: */
	dest.x = 0;
	dest.y = 0;
	dest.w = bitmap->w;
	dest.h = bitmap->h;
	SDL_BlitSurface(bitmap, NULL, screen, &dest);
	SDL_FreeSurface(bitmap);

	if (SDL_MUSTLOCK(screen)) {
		SDL_UnlockSurface(screen);
	}

	dest.x = 0;
	dest.y = 0;
	dest.w = screen->w;
	dest.h = screen->h;
	SDL_UpdateRects(screen, 1, &dest);
}

int main(int argc, char *argv[])
{
	const unsigned width = 640;
	const unsigned height = 480;
	webcam_t *cam;
	buffer_t frame = { NULL, 0 };
	SDL_Surface *screen;
	SDL_Event event;
	int quit = 0;
	struct timeval p_start, p_end;
	int fcnt = 0;

	gettimeofday(&p_start, NULL);

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "Error: can not init SDL.\n");
		return 1;
	}
	atexit(SDL_Quit);

	/* Openning webcam: */
	cam = webcam_open("/dev/video0");
	if (!cam) {
		fprintf(stderr, "Error: can not open webcam!\n");
		return 1;
	}
	webcam_resize(cam, width, height);

	/* Try to open screen: */
	screen = SDL_SetVideoMode(cam->width, cam->height + 256, 24, SDL_SWSURFACE);
	if (!screen) {
		webcam_close(cam);
		fprintf(stderr, "Error: can not set video mode: %s\n", SDL_GetError());
		return 1;
	}

	webcam_stream(cam, true);
	printf("WxH: %dx%d\n", cam->width, cam->height);

	/* Wait for events: */
	for (;!quit;) {
		webcam_grab(cam, &frame);

		/* Draw frame: */
		if (frame.length > 0) {
			++fcnt;
			draw_image(cam, &frame, screen);
		}

		while (SDL_PollEvent(&event)) { /* Check for SDL events */
			switch (event.type) {
				case SDL_QUIT: quit = 1; break;
				case SDL_MOUSEBUTTONDOWN: lineY = event.button.y; break;
			}
		}
	}
	webcam_stream(cam, false);

	gettimeofday(&p_end, NULL);
	if (fcnt) {
		int delta = 1000 * (p_end.tv_sec - p_start.tv_sec) + (p_end.tv_usec - p_start.tv_usec) / 1000;
		float fps = fcnt * 1000.0 / delta;

		printf("%d frames processed in %2.2fsecs (%f FPS)\n", fcnt, delta / 1000.0, fps);
	}

	SDL_FreeSurface(screen);

	return 0;
}


