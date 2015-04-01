# This makefile is for tests only

CFLAGS = -g -O0 -W -Wall -Wno-unused-parameter
all: test test_sdl test_jpeg

test: webcam.c webcam.h
	gcc $(CFLAGS) -DWEBCAM_TEST -o test webcam.c -lpthread

test_sdl: webcam.c webcam.h test_sdl.c
	gcc $(CFLAGS) -o test_sdl `pkg-config sdl --cflags` webcam.c test_sdl.c -lpthread `pkg-config sdl --libs`

test_jpeg: webcam.c webcam.h test_jpeg.c
	gcc $(CFLAGS) -o test_jpeg `pkg-config libjpeg --cflags` webcam.c test_jpeg.c -lpthread `pkg-config libjpeg --libs`

clean:
	rm -f test test_sdl

