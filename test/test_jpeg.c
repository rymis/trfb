#include <libwebcam.h>
#include <stdio.h>
#include <string.h>

#if HAVE_JPEGLIB_H
# include <jpeglib.h>
# define USE_JPEG 1
#else
# define USE_JPEG 0
#endif

#include <stdio.h>
#include <errno.h>

#if USE_JPEG
int save_jpeg(const char *filename, int quality, webcam_color_t *data, int width, int height)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	FILE *outfile;
	union {
		int x;
		unsigned char b[sizeof(int)];
	} be;
	JSAMPROW row_pointer[1];

	be.x = 1;
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);

	outfile = fopen(filename, "wb");
	if (!outfile) {
		fprintf(stderr, "Can't open file: %s\n", strerror(errno));
		return -1;
	}

	jpeg_stdio_dest(&cinfo, outfile);

	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.input_components = 4;
	if (be.b[0]) {
		cinfo.in_color_space = JCS_EXT_BGRX;
	} else {
		cinfo.in_color_space = JCS_EXT_XRGB;
	}

	jpeg_set_defaults(&cinfo);

	jpeg_set_quality(&cinfo, quality, TRUE);

	jpeg_start_compress(&cinfo, TRUE);

	while (cinfo.next_scanline < cinfo.image_height) {
		row_pointer[0] = (void*)&data[cinfo.next_scanline * width];
		(void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	jpeg_finish_compress(&cinfo);

	fclose(outfile);

	jpeg_destroy_compress(&cinfo);

	return 0;
}
#else

/* We don't have any JPEG so lets save pnm :) */
int save_jpeg(const char *filename, int quality, webcam_color_t *data, int width, int height)
{
	FILE *f = fopen(filename, "wb");
	int x, y;
	if (!f) {
		fprintf(stderr, "Can't open file: %s\n", strerror(errno));
		return -1;
	}

	fprintf(f, "P6\n%d %d\n255\n", width, height);
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			fputc((data[y * width + x] >> 16) & 0xff, f);
			fputc((data[y * width + x] >> 8) & 0xff, f);
			fputc((data[y * width + x]) & 0xff, f);
		}
	}

	fclose(f);

	return 0;
}

#endif

int main(int argc, char **argv)
{
	unsigned i = 0;
	char *nm;
	char fn[16];
	int cams[64];
	unsigned cam_cnt = 64;
	webcam_t *w;

	if (webcam_list(cams, &cam_cnt)) {
		fprintf(stderr, "Error: Can't get list of cameras!\n");
		return 1;
	}

	if (cam_cnt == 0) {
		fprintf(stderr, "Error: no cameras connected!\n");
		return 1;
	}

	for (i = 0; i < cam_cnt; i++) {
		nm = webcam_name(cams[i]);
		if (!nm)
			continue;
		printf("CAMERA: %s\n", nm);
		free(nm);
	}

	w = webcam_open(cams[0], 0, 0);
	if (!w) {
		fprintf(stderr, "Error: can't open camera!\n");
		return 1;
	}

	printf("Image size is %ux%u\n", w->width, w->height);

	webcam_start(w);

	for (;;) {
		if (webcam_wait_frame(w, 10) > 0) {
			printf("Storing frame %d\n", i);
			sprintf(fn, "frame_%d.%s", i, USE_JPEG? "jpg": "pnm");

			save_jpeg(fn, 75, w->image, w->width, w->height);
			i++;
		}

		if (i > 10) break;
	}
	webcam_stop(w);
	webcam_close(w);

	return 0;
}
