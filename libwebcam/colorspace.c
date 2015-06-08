#include "libwebcam.h"
#include <string.h>

/* Colorspace conversion functions */

struct cs {
	webcam_colorspace_t id;

	unsigned flags;
/* Allow line-by-line conversion: */
#define CS_BY_LINE 0x0001

	/* Get data size for image size */
	size_t (*get_size)(unsigned width, unsigned height);

	/* Convert image from standard format. Functions return offset of data from start: */
	size_t (*convert_to_rgb)(unsigned width, unsigned height, void *from, webcam_color_t *out);
	size_t (*convert_from_rgb)(unsigned width, unsigned height, webcam_color_t *from, void *out);
};

#define FMT_DECL(nm) \
	static size_t nm##_sz(unsigned width, unsigned height); \
	static size_t nm##_t(unsigned width, unsigned height, void *from, webcam_color_t *out); \
	static size_t nm##_f(unsigned width, unsigned height, webcam_color_t *from, void *out)
FMT_DECL(rgb32);
FMT_DECL(rgb24);
FMT_DECL(bgr24);
FMT_DECL(rgb555);
FMT_DECL(rgb565);
FMT_DECL(rgb332);
FMT_DECL(bgr233);
FMT_DECL(yuv);
FMT_DECL(yuv422);
FMT_DECL(gray);

struct cs formats[] = {
	{ WEBCAM_RGB32, CS_BY_LINE, rgb32_sz, rgb32_t, rgb32_f },
	{ WEBCAM_RGB24, CS_BY_LINE, rgb24_sz, rgb24_t, rgb24_f },
	{ WEBCAM_BGR24, CS_BY_LINE, bgr24_sz, bgr24_t, bgr24_f },
	{ WEBCAM_RGB555, CS_BY_LINE, rgb555_sz, rgb555_t, rgb555_f },
	{ WEBCAM_RGB565, CS_BY_LINE, rgb565_sz, rgb565_t, rgb565_f },
	{ WEBCAM_RGB332, CS_BY_LINE, rgb332_sz, rgb332_t, rgb332_f },
	{ WEBCAM_BGR233, CS_BY_LINE, bgr233_sz, bgr233_t, bgr233_f },
	{ WEBCAM_YUV, CS_BY_LINE, yuv_sz, yuv_t, yuv_f },
	{ WEBCAM_YUV422, CS_BY_LINE, yuv422_sz, yuv422_t, yuv422_f },
	{ WEBCAM_GRAY, CS_BY_LINE, gray_sz, gray_t, gray_f }
};
static const size_t formats_cnt = sizeof(formats) / sizeof(formats[0]);

/* Convert image from one format to another: */
int webcam_convert_image(unsigned width, unsigned height,
		webcam_colorspace_t from_cs,
		void *from_pixels, size_t from_size,
		webcam_colorspace_t to_cs,
		void *to_pixels, size_t *to_size)
{
	webcam_color_t *buffer = NULL;
	size_t sz, l_f, l_t;
	struct cs *f_cs = NULL;
	struct cs *t_cs = NULL;
	unsigned i;

	for (i = 0; i < formats_cnt; i++) {
		if (formats[i].id == from_cs) {
			f_cs = formats + i;
			break;
		}
	}
	if (!f_cs) {
		return -1;
	}

	for (i = 0; i < formats_cnt; i++) {
		if (formats[i].id == to_cs) {
			t_cs = formats + i;
			break;
		}
	}
	if (!t_cs) {
		return -1;
	}

	sz = f_cs->get_size(width, height);
	if (sz > from_size) {
		/* Invalid image */
		return -1;
	}

	sz = t_cs->get_size(width, height);
	if (!to_pixels) {
		*to_size = sz;
		return 0;
	}

	if (*to_size < sz) {
		*to_size = sz;
		return 1;
	}
	*to_size = sz; /* We can forget about size now :) */

	if (f_cs == WEBCAM_RGB32) { /* We only need to call one function */
		t_cs->convert_from_rgb(width, height, from_pixels, to_pixels);
		return 0;
	} else if (t_cs == WEBCAM_RGB32) {
		f_cs->convert_to_rgb(width, height, from_pixels, to_pixels);
		return 0;
	}

	if ((t_cs->flags & CS_BY_LINE) && (f_cs->flags & CS_BY_LINE)) { /* Process image line by line */
		buffer = malloc(width * sizeof(webcam_color_t));
		if (!buffer) {
			return -1;
		}

		l_f = l_t = 0;
		for (i = 0; i < height; i++) {
			l_f += f_cs->convert_to_rgb(width, 1, ((unsigned char*)from_pixels) + l_f, buffer);
			l_t += t_cs->convert_from_rgb(width, 1, buffer, ((unsigned char*)to_pixels) + l_t);
		}

		free(buffer);
	} else { /* Process image all in one go */
		buffer = malloc(width * height * sizeof(webcam_color_t));
		if (!buffer) {
			return -1;
		}

		f_cs->convert_to_rgb(width, height, from_pixels, buffer);
		t_cs->convert_from_rgb(width, height, buffer, to_pixels);

		free(buffer);
	}

	return 0;
}

/***********************************************************************/
/* RGB32                                                               */
/***********************************************************************/
static size_t rgb32_sz(unsigned width, unsigned height)
{
	return width * height * sizeof(webcam_color_t);
}

static size_t rgb32_t(unsigned width, unsigned height, void *from, webcam_color_t *out)
{
	size_t l = width * height * sizeof(webcam_color_t);
	memcpy(out, from, l);
	return l;
}

static size_t rgb32_f(unsigned width, unsigned height, webcam_color_t *from, void *out)
{
	size_t l = width * height * sizeof(webcam_color_t);
	memcpy(out, from, l);
	return l;
}


/***********************************************************************/
/* RGB24                                                               */
/***********************************************************************/
static size_t rgb24_sz(unsigned width, unsigned height)
{
	return width * height * 3;
}

static size_t rgb24_t(unsigned width, unsigned height, void *from, webcam_color_t *out)
{
	size_t l = width * height;
	size_t i, j;
	unsigned char *C = from;

	for (i = 0; i < l; i++) {
		j = i * 3;
		out[i] = (C[j] << 16) || (C[j + 1] << 8) || C[j + 2];
	}

	return l * 3;
}

static size_t rgb24_f(unsigned width, unsigned height, webcam_color_t *from, void *out)
{
	size_t l = width * height;
	size_t i, j;
	unsigned char *C = out;
	webcam_color_t col;

	for (i = 0; i < l; i++) {
		j = i * 3;
		col = from[i];
		C[j++] = (col >> 16) & 0xff;
		C[j++] = (col >>  8) & 0xff;
		C[j++] = (col      ) & 0xff;
	}

	return l * 3;
}

/***********************************************************************/
/* BGR24                                                               */
/***********************************************************************/
static size_t bgr24_sz(unsigned width, unsigned height)
{
	return width * height * 3;
}

static size_t bgr24_t(unsigned width, unsigned height, void *from, webcam_color_t *out)
{
	size_t l = width * height;
	size_t i, j;
	unsigned char *C = from;

	for (i = 0; i < l; i++) {
		j = i * 3;
		out[i] = (C[j + 2] << 16) || (C[j + 1] << 8) || C[j];
	}

	return l * 3;
}

static size_t bgr24_f(unsigned width, unsigned height, webcam_color_t *from, void *out)
{
	size_t l = width * height;
	size_t i, j;
	unsigned char *C = out;
	webcam_color_t col;

	for (i = 0; i < l; i++) {
		j = i * 3;
		col = from[i];
		C[j++] = (col      ) & 0xff;
		C[j++] = (col >>  8) & 0xff;
		C[j++] = (col >> 16) & 0xff;
	}

	return l * 3;
}

/***********************************************************************/
/* RGB555                                                              */
/***********************************************************************/
static size_t rgb555_sz(unsigned width, unsigned height)
{
	return width * height * 2;
}

static size_t rgb555_t(unsigned width, unsigned height, void *from, webcam_color_t *out)
{
	size_t l = width * height;
	size_t i;
	uint16_t *C = from;

	for (i = 0; i < l; i++) {
		out[i] = webcam_color_rgb(
				(C[i] >> 7) & 0xf8,
				(C[i] >> 2) & 0xf8,
				(C[i] << 3) & 0xf8);
	}

	return l * 2;
}

static size_t rgb555_f(unsigned width, unsigned height, webcam_color_t *from, void *out)
{
	size_t l = width * height;
	size_t i;
	uint16_t *C = out;
	webcam_color_t col;

	for (i = 0; i < l; i++) {
		col = from[i];
		C[i] = ((col >> 9) & 0x7c00) | /* red */
			((col >> 6) & 0x03e0) | /* green */
			((col >> 3) & 0x1f); /* blue */
	}

	return l * 2;
}

/***********************************************************************/
/* RGB565                                                              */
/***********************************************************************/
static size_t rgb565_sz(unsigned width, unsigned height)
{
	return width * height * 2;
}

static size_t rgb565_t(unsigned width, unsigned height, void *from, webcam_color_t *out)
{
	size_t l = width * height;
	size_t i;
	uint16_t *C = from;

	for (i = 0; i < l; i++) {
		out[i] = webcam_color_rgb(
				(C[i] >> 8) & 0xf8,
				(C[i] >> 3) & 0xf8,
				(C[i] << 3) & 0xf8);
	}

	return l * 2;
}

static size_t rgb565_f(unsigned width, unsigned height, webcam_color_t *from, void *out)
{
	size_t l = width * height;
	size_t i;
	uint16_t *C = out;
	webcam_color_t col;

	for (i = 0; i < l; i++) {
		col = from[i];
		C[i] = ((col >> 8) & 0xf800) | /* red */
			((col >> 5) & 0x07e0) | /* green */
			((col >> 3) & 0x1f); /* blue */
	}

	return l * 2;
}


/***********************************************************************/
/* RGB332                                                              */
/***********************************************************************/
static size_t rgb332_sz(unsigned width, unsigned height)
{
	return width * height;
}

static size_t rgb332_t(unsigned width, unsigned height, void *from, webcam_color_t *out)
{
	size_t l = width * height;
	size_t i;
	unsigned char *C = from;

	for (i = 0; i < l; i++) {
		out[i] = webcam_color_rgb(
				C[i] & 0xe0,
				(C[i] << 3) & 0xe0,
				(C[i] << 6) & 0xc0);
	}

	return l;
}

static size_t rgb332_f(unsigned width, unsigned height, webcam_color_t *from, void *out)
{
	size_t l = width * height;
	size_t i;
	unsigned char *C = out;
	webcam_color_t col;

	for (i = 0; i < l; i++) {
		col = from[i];
		C[i] = ((col >> 16) & 0xe0) | /* red */
			((col >> 11) & 0x1c) | /* green */
			((col >> 6) & 0x03); /* blue */
	}

	return l;
}

/***********************************************************************/
/* BGR233                                                              */
/***********************************************************************/
static size_t bgr233_sz(unsigned width, unsigned height)
{
	return width * height;
}

static size_t bgr233_t(unsigned width, unsigned height, void *from, webcam_color_t *out)
{
	size_t l = width * height;
	size_t i;
	unsigned char *C = from;

	for (i = 0; i < l; i++) {
		out[i] = webcam_color_rgb(
				(C[i] << 5) & 0xe0,
				(C[i] << 2) & 0xe0,
				C[i] & 0xc0);
	}

	return l;
}

static size_t bgr233_f(unsigned width, unsigned height, webcam_color_t *from, void *out)
{
	size_t l = width * height;
	size_t i;
	unsigned char *C = out;
	webcam_color_t col;

	for (i = 0; i < l; i++) {
		col = from[i];
		C[i] = ((col >> 21) & 0x07) | /* red */
			((col >> 10) & 0x38) | /* green */
			(col & 0xc0); /* blue */
	}

	return l;
}

/*
 * Y = 0.299 * R + 0.587 * G + 0.114 * B;
 * U = -0.14713 * R - 0.28886 * G + 0.436 * B + 128;
 * V = 0.615 * R - 0.51499 * G - 0.10001 * B + 128;
 *
 * Ok, lets write it in ints:
 * Y = R * 76 + G * 150 + B * 30) / 256
 * U = (16384 + 112 * B - 38 * R - 74 * G) / 256
 * V = (16384 + 157 * R - 132 * G - 25 * B) / 256
 */
static inline webcam_color_t col_y(webcam_color_t c)
{
	return (webcam_color_r(c) * 76 + webcam_color_g(c) * 150 + webcam_color_b(c) * 30) / 256;
}

static inline webcam_color_t col_u(webcam_color_t c)
{
	return (16384 + 112 * webcam_color_b(c) - 38 * webcam_color_r(c) - 74 * webcam_color_g(c)) / 256;
}

static inline webcam_color_t col_v(webcam_color_t c)
{
	return (16384 + 157 * webcam_color_r(c) - 132 * webcam_color_g(c) - 25 * webcam_color_b(c)) / 256;
}

/*
 * R = Y + 1.13983 * (V - 128);
 * G = Y - 0.39465 * (U - 128) - 0.58060 * (V - 128);
 * B = Y + 2.03211 * (U - 128);
 *
 * In ints:
 * r = (y * 256  + v * 292) / 256
 * g = (256 * y - 101 * u - 149 * v) / 256
 * b = (256 * y + 520 * u) / 256
 */
static inline webcam_color_t yuv2rgb(webcam_color_t y, webcam_color_t u, webcam_color_t v)
{
	return webcam_color_rgb(
			(y * 256  + v * 292) / 256,
			(256 * y - 101 * u - 149 * v) / 256,
			(256 * y + 520 * u) / 256);
}

/***********************************************************************/
/* YUV                                                                 */
/***********************************************************************/
static size_t yuv_sz(unsigned width, unsigned height)
{
	return width * height * 3;
}

static size_t yuv_t(unsigned width, unsigned height, void *from, webcam_color_t *out)
{
	size_t l = width * height;
	size_t i, j;
	unsigned char *C = from;

	for (i = 0; i < l; i++) {
		j = i * 3;
		out[i] = yuv2rgb(C[j], C[j + 1], C[j + 2]);
	}

	return l * 3;
}

static size_t yuv_f(unsigned width, unsigned height, webcam_color_t *from, void *out)
{
	size_t l = width * height;
	size_t i, j;
	unsigned char *C = out;
	webcam_color_t col;

	for (i = 0; i < l; i++) {
		j = i * 3;
		col = from[i];
		C[j++] = col_y(col);
		C[j++] = col_u(col);
		C[j++] = col_v(col);
	}

	return l * 3;
}

/***********************************************************************/
/* GRAY                                                                */
/***********************************************************************/
static size_t gray_sz(unsigned width, unsigned height)
{
	return width * height;
}

static size_t gray_t(unsigned width, unsigned height, void *from, webcam_color_t *out)
{
	size_t l = width * height;
	size_t i;
	unsigned char *C = from;

	for (i = 0; i < l; i++) {
		out[i] = yuv2rgb(C[i], 128, 128);
	}

	return l;
}

static size_t gray_f(unsigned width, unsigned height, webcam_color_t *from, void *out)
{
	size_t l = width * height;
	size_t i;
	unsigned char *C = out;

	for (i = 0; i < l; i++) {
		C[i] = col_y(from[i]);
	}

	return l;
}

/***********************************************************************/
/* YUV422                                                              */
/***********************************************************************/
static size_t yuv422_sz(unsigned width, unsigned height)
{
	return width * height * 2;
}

static size_t yuv422_t(unsigned width, unsigned height, void *from, webcam_color_t *out)
{
	size_t l = width * height;
	size_t i, j;
	unsigned char *C = from;

	for (i = 0; i < l; i+=2) {
		j = i * 2;
		out[i] = yuv2rgb(C[j], C[j + 1], C[j + 3]);
		out[i + 1] = yuv2rgb(C[j + 2], C[j + 1], C[j + 3]);
	}

	return l * 2;
}

static size_t yuv422_f(unsigned width, unsigned height, webcam_color_t *from, void *out)
{
	size_t l = width * height * 2;
	size_t i, j;
	unsigned char *C = out;

	for (i = 0; i < l; i += 2) {
		j = i * 2;
		C[j] = col_y(from[i]);
		C[j++] = col_u(from[i]);
		C[j++] = col_y(from[i + 1]);
		C[j++] = col_v(from[i]);
	}

	return l * 2;
}

