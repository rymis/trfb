#include <libwebcam.h>
#include <stdio.h>
#include <string.h>
#include <jpeglib.h>
#include <stdio.h>
#include <errno.h>

int save_jpeg(const char *filename, int quality, unsigned char *data, int width, int height)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	FILE *outfile;
	JSAMPROW row_pointer[1];
	int row_stride;
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
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;

	jpeg_set_defaults(&cinfo);

	jpeg_set_quality(&cinfo, quality, TRUE);

	jpeg_start_compress(&cinfo, TRUE);

	row_stride = width * 3;

	while (cinfo.next_scanline < cinfo.image_height) {
		row_pointer[0] = &data[cinfo.next_scanline * row_stride];
		(void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	jpeg_finish_compress(&cinfo);

	fclose(outfile);

	jpeg_destroy_compress(&cinfo);

	return 0;
}

int main(int argc, char **argv)
{
    int i = 0;
    webcam_t *w = webcam_open(0, 0, 0);
    char fn[16];

    webcam_start(w);

    for (;;) {
        if (webcam_wait_frame(w, 10) > 0) {
            printf("Storing frame %d\n", i);
            sprintf(fn, "frame_%d.jpg", i);

	    save_jpeg(fn, 75, w->img, w->width, w->height);
            i++;
        }

        if (i > 10) break;
    }
    webcam_stop(w);
    webcam_close(w);

    return 0;
}
