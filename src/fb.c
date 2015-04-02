#include <trfb.h>
#include <string.h>
#include <stdlib.h>

static int isBE(void)
{
	union {
		unsigned value;
		unsigned char data[sizeof(unsigned)];
	} test;

	test.value = 1;

	return !test.data[0];
}

static unsigned char norm_from_mask(unsigned char mask)
{
	unsigned c = mask;
	unsigned r = 0;

	if (!c) {
		return 0;
	}

	for (;;) {
		c <<= 1;
		if (c >= 0x100)
			return r;
		r++;
	}

	/* not reached */
	return 0;
}

/* Framebuffer support */
trfb_framebuffer_t* trfb_framebuffer_create(unsigned width, unsigned height, unsigned char bpp)
{
	unsigned sz = width * height;
	trfb_framebuffer_t *fb;

	if (width > 0xffff || height > 0xffff) {
		trfb_msg("Trying to create framebuffer of size %ux%u", width, height);
		return NULL;
	}

	fb = calloc(1, sizeof(trfb_framebuffer_t));
	if (!fb) {
		trfb_msg("Not enought memory");
		return NULL;
	}

	fb->bpp = bpp;
	fb->width = width;
	fb->height = height;
	if (bpp == 1) {
		fb->pixels = calloc(1, sz);
		fb->rmask = TRFB_FB8_RMASK;
		fb->gmask = TRFB_FB8_GMASK;
		fb->bmask = TRFB_FB8_BMASK;
		fb->rshift = TRFB_FB8_RSHIFT;
		fb->gshift = TRFB_FB8_GSHIFT;
		fb->bshift = TRFB_FB8_BSHIFT;
	} else if (bpp == 2) {
		fb->pixels = calloc(2, sz);
		fb->rmask = TRFB_FB16_RMASK;
		fb->gmask = TRFB_FB16_GMASK;
		fb->bmask = TRFB_FB16_BMASK;
		fb->rshift = TRFB_FB16_RSHIFT;
		fb->gshift = TRFB_FB16_GSHIFT;
		fb->bshift = TRFB_FB16_BSHIFT;
	} else if (bpp == 4) {
		fb->pixels = calloc(4, sz);
		fb->rmask = TRFB_FB32_RMASK;
		fb->gmask = TRFB_FB32_GMASK;
		fb->bmask = TRFB_FB32_BMASK;
		fb->rshift = TRFB_FB32_RSHIFT;
		fb->gshift = TRFB_FB32_GSHIFT;
		fb->bshift = TRFB_FB32_BSHIFT;
	} else {
		trfb_msg("Only 1, 2 and 4 bytes per pixel is supported");
		free(fb);
		return NULL;
	}

	fb->rnorm = norm_from_mask(fb->rmask);
	fb->gnorm = norm_from_mask(fb->gmask);
	fb->bnorm = norm_from_mask(fb->bmask);

	if (!fb->pixels) {
		trfb_msg("Not enought memory");
		free(fb);
		return NULL;
	}

	if (mtx_init(&fb->lock, mtx_plain) != thrd_success) {
		free(fb->pixels);
		free(fb);
		trfb_msg("Can't create mutex");
		return NULL;
	}

	return fb;
}

trfb_framebuffer_t* trfb_framebuffer_copy(trfb_framebuffer_t *fb)
{
	trfb_framebuffer_t *c;
	size_t len;

	c = malloc(sizeof(trfb_framebuffer_t));
	if (!c) {
		return NULL;
	}

	memcpy(c, fb, sizeof(trfb_framebuffer_t));
	if (fb->bpp != 1 && fb->bpp != 2 && fb->bpp != 4) {
		free(c);
		return NULL;
	}

	len = fb->width * fb->height * fb->bpp;
	c->pixels = malloc(len);
	if (!c->pixels) {
		free(c);
		return NULL;
	}

	memcpy(c->pixels, fb->pixels, len);

	return c;
}

void trfb_framebuffer_free(trfb_framebuffer_t *fb)
{
	if (fb) {
		free(fb->pixels);
		mtx_destroy(&fb->lock);
		free(fb);
	}
}

int trfb_framebuffer_resize(trfb_framebuffer_t *fb, unsigned width, unsigned height)
{
	unsigned W, H;
	unsigned y;

	if (!fb || !width || !height || width > 0xffff || height > 0xffff) {
		trfb_msg("Invalid params to resize");
		return -1;
	}

	W = width < fb->width? width : fb->width;
	H = height < fb->height? height : fb->height;

#define FB_COPY(tp) \
	do { \
		tp *p = fb->pixels; \
		tp *np; \
		np = calloc(width * height, sizeof(tp)); \
		if (!np) { \
			trfb_msg("Not enought memory!"); \
			return -1; \
		} \
		for (y = 0; y < H; y++) \
			memcpy(np + y * width, p + y * fb->width, W * sizeof(tp)); \
	} while (0)

	if (fb->bpp == 1) {
		FB_COPY(uint8_t);
	} else if (fb->bpp == 2) {
		FB_COPY(uint16_t);
	} else if (fb->bpp == 4) {
		FB_COPY(uint32_t);
	} else {
		trfb_msg("Invalid framebuffer: bpp = %d", fb->bpp);
		return -1;
	}

	fb->width = width;
	fb->height = height;

	return 0;
}

/* This is the main function for us */
int trfb_framebuffer_convert(trfb_framebuffer_t *dst, trfb_framebuffer_t *src)
{
	unsigned y, x;

	if (!dst || !src) {
		trfb_msg("Invalid arguments");
		return -1;
	}

	if (src->bpp != 1 && src->bpp != 2 && src->bpp != 4) {
		trfb_msg("Invalid framebuffer: BPP = %d", src->bpp);
		return -1;
	}

	if (dst->bpp != 1 && dst->bpp != 2 && dst->bpp != 4) {
		trfb_msg("Invalid framebuffer: BPP = %d", dst->bpp);
		return -1;
	}

	if (!src->width || !src->height || !dst->width || !dst->height ||
			src->width > 0xffff || src->height > 0xffff ||
			dst->width > 0xffff || dst->height > 0xffff) {
		trfb_msg("Framebuffer of invalid size");
		return -1;
	}

	/* If image has another size we need to change it: */
	if (dst->width != src->width || dst->height != src->height) {
		void *p = realloc(dst->pixels, src->width * src->height * dst->bpp);
		if (!p) {
			trfb_msg("Not enought memory!");
			return -1;
		}
		dst->pixels = p;
		dst->width = src->width;
		dst->height = src->height;
	}

	if (
		 dst->bpp == src->bpp &&
		 dst->rmask == src->rmask &&
		 dst->gmask == src->gmask &&
		 dst->bmask == src->bmask &&
		 dst->rshift == src->rshift &&
		 dst->gshift == src->gshift &&
		 dst->bshift == src->bshift
	   ) { /* Format is the same! */
		memcpy(dst->pixels, src->pixels, src->width * src->height * dst->bpp);
		return 0;
	}

	/* Ok, so we need to do it slow way... */
	/* TODO: it is too slow */
	for (y = 0; y < src->height; y++) {
		for (x = 0; x < src->width; x++) {
			trfb_framebuffer_set_pixel(dst, x, y, trfb_framebuffer_get_pixel(src, x, y));
		}
	}

	return 0;
}

int trfb_framebuffer_format(trfb_framebuffer_t *fb, trfb_format_t *fmt)
{
	if (!fb || !fmt) {
		trfb_msg("Invalid arguments");
		return -1;
	}

	fmt->bpp = fb->bpp * 8;
	fmt->big_endian = isBE();
	if (fb->bpp == 1) {
		fmt->depth = 8;
	} else if (fb->bpp == 2) {
		fmt->depth = 16;
	} else {
		fmt->depth = 24;
	}
	if (fb->bpp == 1 && !fb->rmask) {
		fmt->true_color = 0;
	} else {
		fmt->true_color = 1;
	}
	fmt->rmax = fb->rmask;
	fmt->gmax = fb->gmask;
	fmt->bmax = fb->bmask;
	fmt->rshift = fb->rshift;
	fmt->gshift = fb->gshift;
	fmt->bshift = fb->bshift;

	return 0;
}

trfb_framebuffer_t* trfb_framebuffer_create_of_format(unsigned width, unsigned height, trfb_format_t *fmt)
{
	trfb_framebuffer_t *fb;

	if (!fmt) {
		trfb_msg("Invalid arguments");
		return NULL;
	}

	if (fmt->bpp != 8 && fmt->bpp != 16 && fmt->bpp != 32) {
		trfb_msg("Invalid format: BPP = %d", fmt->bpp);
		return NULL;
	}

	fb = trfb_framebuffer_create(width, height, fmt->bpp / 8);
	if (!fb) {
		return NULL;
	}

	fb->rmask = fmt->rmax;
	fb->gmask = fmt->gmax;
	fb->bmask = fmt->bmax;
	fb->rshift = fmt->rshift;
	fb->gshift = fmt->gshift;
	fb->bshift = fmt->bshift;

	return fb;
}

void trfb_framebuffer_endian(trfb_framebuffer_t *fb, int is_be)
{
	size_t i;
	size_t len;
	unsigned char tmp;
	unsigned char *p;

#define cswap(x, y) \
	do { \
		tmp = x; \
		x = y; \
		y = tmp; \
	} while (0)

	if (!fb)
		return;
	if (fb->bpp == 1)
		return;
	if (isBE() && is_be)
		return;
	if (!isBE() && !is_be)
		return;

	p = fb->pixels;
	if (fb->bpp == 2) {
		len = fb->width * fb->height * 2;
		for (i = 0; i < len; i += 2) {
			cswap(p[i], p[i + 1]);
		}
	} else if (fb->bpp == 4) {
		len = fb->width * fb->height * 4;
		for (i = 0; i < len; i += 4) {
			cswap(p[i], p[i + 3]);
			cswap(p[i + 1], p[i + 2]);
		}
	}
}


