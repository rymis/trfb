#include "libwebcam.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <linux/videodev2.h>
#include <libv4l2.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/select.h>
#include <stdarg.h>

#define IO_METHOD_MMAP 1
#define IO_METHOD_USER 2
#define IO_METHOD_READ 3

static void log_full(int line, const char *fmt, ...)
{
	va_list args;
	char buf[256];
	int i;

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	for (i = 0; buf[i]; i++)
		if (buf[i] < 32)
			buf[i] = ' ';
	fprintf(stderr, "[%d] %s\n", line, buf);
}

#define log(...) log_full(__LINE__, __VA_ARGS__)

struct buffer {
	unsigned char *start;
	size_t len;
};

typedef struct webcam_private {
	int fd;
	int io_method;

	struct buffer *buffers;
	unsigned buffers_count;

	unsigned char *buf; /* read buffer */
	size_t img_len;
	size_t linebytes;
} priv_t;

static int init_cam(webcam_t *cam, const char *devname);
static void process_image(webcam_t *cam, unsigned char *img, size_t img_len);

/* Count all cameras connected */
int webcam_list(int *ids, unsigned *count)
{
	int i;
	unsigned cnt = 0;
	char namebuf[128];
	struct stat st;
	unsigned len;

	if (!count) {
		return -1;
	}

	len = *count;

	for (i = 0; i < 64; i++) {
		snprintf(namebuf, sizeof(namebuf), "/dev/video%d", i);
		if (stat(namebuf, &st) == 0) {
			if (S_ISCHR(st.st_mode)) {
				if (ids) {
					if (cnt >= len) {
						return -1;
					}
					ids[cnt++] = i;
				} else {
					++cnt;
				}
			}
		}
	}

	*count = cnt;

	return 0;
}

#define REINTR(rv, some) \
	do { \
		rv = (some); \
	} while (rv < 0 && (errno == EINTR || errno == EAGAIN))

char* webcam_name(int id)
{
	char namebuf[128];
	char info[512];
	int fd;
	struct v4l2_capability cap;
	int rv;

	if (id < 0 || id >= 64) {
		log("Invalid camera ID");
		return NULL;
	}

	snprintf(namebuf, sizeof(namebuf), "/dev/video%d", id);
	fd = v4l2_open(namebuf, O_RDWR | O_NONBLOCK, 0);
	if (fd < 0) {
		log("Can't open `%s' (%s)", namebuf, strerror(errno));
		return NULL;
	}

	/* Request for capabilities: */
	REINTR(rv, v4l2_ioctl(fd, VIDIOC_QUERYCAP, &cap));
	if (rv < 0) {
		log("ioctl failed");
		v4l2_close(fd);
		return NULL;
	}

	snprintf(info, sizeof(info), "%s [%s] %s: %s", cap.bus_info, cap.driver, namebuf, cap.card);

	close(fd);

	return strdup(info);
}

/* Try to open camera with number =num. Width and height is recomended values so you must look inside webcam_t for actual sizes. */
webcam_t* webcam_open(int id, unsigned width, unsigned height)
{
	char namebuf[128];
	struct stat st;
	webcam_t *res;
	priv_t *priv;

	if (id < 0 || id >= 64) {
		log(" Invalid ID ");
		return NULL;
	}

	snprintf(namebuf, sizeof(namebuf), "/dev/video%d", id);
	if (stat(namebuf, &st) != 0 || !S_ISCHR(st.st_mode)) {
		log("Can't find device: %d", id);
		return NULL;
	}

	if (width == 0) {
		width = 640;
		height = 480;
	}

	/* Ok now we can allocate webcam structure and open device: */
	res = calloc(1, sizeof(webcam_t));
	if (!res) {
		log("Not enought memory");
		return NULL;
	}

	res->width = width;
	res->height = height;
	/* res->image = calloc(width * height, sizeof(uint32_t)); */

	priv = res->priv = calloc(1, sizeof(priv_t));
	if (!priv) {
		log("Not enought memory");
		free(res);
		return NULL;
	}

	priv->fd = v4l2_open(namebuf, O_RDWR | O_NONBLOCK, 0);
	if (priv->fd < 0) {
		log("Can't open device `%s' (%s)", namebuf, strerror(errno));
		free(priv);
		free(res);
		return NULL;
	}
	priv->io_method = IO_METHOD_MMAP;

	if (init_cam(res, namebuf)) {
		close(priv->fd);
		priv->fd = v4l2_open(namebuf, O_RDWR | O_NONBLOCK, 0);
		if (priv->fd < 0) {
			log("Can't open device `%s' (%s)", namebuf, strerror(errno));
			free(priv);
			free(res);
			return NULL;
		}
		priv->io_method = IO_METHOD_READ;

		if (init_cam(res, namebuf)) {
			log("Can not init device: `%s'", namebuf);
			close(priv->fd);
			free(priv);
			free(res);
			return NULL;
		}
	}

	return res;
}

/* Close device and free resources */
void webcam_close(webcam_t *cam)
{
	priv_t *priv;
	unsigned i;

	if (!cam)
		return;

	priv = cam->priv;
	if (!priv) {
		log("Invalid webcam!");
		return;
	}

	if (priv->io_method == IO_METHOD_MMAP) {
		for (i = 0; i < priv->buffers_count; i++)
			v4l2_munmap(priv->buffers[i].start, priv->buffers[i].len);
	} else {
		free(priv->buf);
	}
	free(priv->buffers);
	priv->buffers = NULL;
	priv->buffers_count = 0;

	v4l2_close(priv->fd);
	priv->fd = -1;
	free(cam->image);

	free(priv);
	free(cam);
}

/* Enable capturing: */
int webcam_start(webcam_t *cam)
{
	struct v4l2_buffer buf;
	enum v4l2_buf_type type;
	unsigned i;
	int rv;
	priv_t *priv;

	if (!cam || !cam->priv) {
		log("INVAL");
		return -1;
	}
	priv = cam->priv;

	if (priv->io_method == IO_METHOD_MMAP) {
		for (i = 0; i < priv->buffers_count; i++) {
			memset(&buf, 0, sizeof(buf));

			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;
			buf.index = i;

			REINTR(rv, v4l2_ioctl(priv->fd, VIDIOC_QBUF, &buf));
			if (rv < 0) {
				log("VIDIOC_QBUF failed");
				return -1;
			}
		}

		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		REINTR(rv, v4l2_ioctl(priv->fd, VIDIOC_STREAMON, &type));
		if (rv < 0) {
			log("streaming failed");
			return -1;
		}
	} else if (priv->io_method == IO_METHOD_READ) {
		/* nothing to do */
	} else {
		log("Unsupported IO method");
		return -1;
	}

	return 0;
}

/* Disable capturing: */
int webcam_stop(webcam_t *cam)
{
	priv_t *priv;
	enum v4l2_buf_type type;
	int rv;

	if (!cam || !cam->priv) {
		log("INVAL");
		return -1;
	}
	priv = cam->priv;

	if (priv->io_method == IO_METHOD_READ) {
		/* nothing to do */
	} else if (priv->io_method == IO_METHOD_MMAP) {
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		REINTR(rv, v4l2_ioctl(priv->fd, VIDIOC_STREAMOFF, &type));
		if (rv < 0) {
			log("VIDIOC_STREAMOFF");
			return -1;
		}
	} else {
		log("Unsupported IO method");
		return -1;
	}

	return 0;
}

/* Wait for next frame for maximum =delay ms (0 = forever) */
int webcam_wait_frame(webcam_t *cam, unsigned delay)
{
	struct v4l2_buffer buf;
	priv_t *priv;
	fd_set fds;
	struct timeval tv;
	int rv;

	if (!cam || !cam->priv) {
		return -1;
	}
	priv = cam->priv;

	FD_ZERO(&fds);
	FD_SET(priv->fd, &fds);

	tv.tv_sec = delay / 1000;
	tv.tv_usec = (delay % 1000) * 1000;

	REINTR(rv, select(priv->fd + 1, &fds, NULL, NULL, &tv));
	if (rv < 0) {
		log("select failed");
		return -1;
	}

	if (rv == 0) { /* timeout */
		return 0;
	}

	/* Read frame: */
	if (priv->io_method == IO_METHOD_READ) {
		REINTR(rv, v4l2_read(priv->fd, priv->buf, priv->img_len));
		if (rv < 0) {
			log("read error");
			return -1;
		}

		process_image(cam, priv->buf, priv->img_len);
	} else if (priv->io_method == IO_METHOD_MMAP) {
		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;

		REINTR(rv, v4l2_ioctl(priv->fd, VIDIOC_DQBUF, &buf));
		if (rv < 0) {
			log("VIDIOC_DQBUF");
			return -1;
		}

		if (buf.index >= priv->buffers_count) {
			log("invalid VIDIOC_DQBUF result");
			return -1;
		}

		if (priv->buffers[buf.index].len > priv->img_len) {
			REINTR(rv, v4l2_ioctl(priv->fd, VIDIOC_QBUF, &buf));
			log("Invalid frame");
			return -1;
		}
		process_image(cam, priv->buffers[buf.index].start, priv->buffers[buf.index].len);

		REINTR(rv, v4l2_ioctl(priv->fd, VIDIOC_QBUF, &buf));
	} else {
		log("Invalid IO method");
		return -1;
	}

	return 1;
}

/* Set control to camera. Value must be in interval [0-100] */
static int id_conv(int id)
{
	switch (id) {
		case WEBCAM_BRIGHTNESS:
			return V4L2_CID_BRIGHTNESS;
		case WEBCAM_CONTRAST:
			return V4L2_CID_CONTRAST;
		case WEBCAM_SATURATION:
			return V4L2_CID_SATURATION;
		case WEBCAM_GAMMA:
			return V4L2_CID_GAMMA;
		default:
			return -1;
	}
	return -1;
}

int webcam_set_control(webcam_t *cam, webcam_controls_t id, int value)
{
	priv_t *priv;
	int v = (value * 16384) / 25;
	int c = id_conv(id);

	if (!cam || !cam->priv || c < 0) {
		return -1;
	}
	priv = cam->priv;

	return v4l2_set_control(priv->fd, c, v);
}

int webcam_get_control(webcam_t *cam, webcam_controls_t id)
{
	priv_t *priv;
	int v;
	int c = id_conv(id);

	if (!cam || !cam->priv || c < 0) {
		return -1;
	}
	priv = cam->priv;

	v = v4l2_get_control(priv->fd, c);
	if (v < 0) {
		return -1;
	}

	return (v * 25) / 16384;
}

static int init_mmap(webcam_t *cam);
static int init_read(webcam_t *cam);
static int init_cam(webcam_t *cam, const char *devname)
{
	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
        struct v4l2_crop crop;
	struct v4l2_format fmt;
	struct v4l2_fmtdesc fmtdesc;
        unsigned min;
	priv_t *priv = cam->priv;
	int rv;
	unsigned i;
	char info[512];

	/* Request for capabilities: */
	REINTR(rv, v4l2_ioctl(priv->fd, VIDIOC_QUERYCAP, &cap));
	if (rv < 0) {
		log("ioctl failed");
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		log("it is not video capture device");
		return -1;
	}

	snprintf(info, sizeof(info), "%s [%s] %s: %s", cap.bus_info, cap.driver, devname, cap.card);

	if (priv->io_method == IO_METHOD_MMAP) {
		if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
			return -1;
		}
	} else if (priv->io_method == IO_METHOD_READ) {
		if (!(cap.capabilities & V4L2_CAP_READWRITE)) { /* usig READ */
			return -1;
		}
	} else {
		log("Can't get frames");
		return -1;
	}

	/* Selecting video input and video standard: */
	memset(&cropcap, 0, sizeof(cropcap));
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	REINTR(rv, v4l2_ioctl(priv->fd, VIDIOC_CROPCAP, &cropcap));
	if (rv == 0) {
		memset(&crop, 0, sizeof(crop));
		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		crop.c = cropcap.defrect;

		REINTR(rv, v4l2_ioctl(priv->fd, VIDIOC_S_CROP, &crop));
		if (rv < 0) {
			log("WARN: cropping is not supported");
		}
	}

	/* Checking all formats: */
	for (i = 0;; i++) {
		char sf[6];
		fmtdesc.index = i;
		fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		REINTR(rv, v4l2_ioctl(priv->fd, VIDIOC_ENUM_FMT, &fmtdesc));
		if (rv < 0) {
			break;
		}

		memset(sf, 0, 5);
		memcpy(sf, &fmtdesc.pixelformat, 4);

/* TODO: determine format to use */
	}

	/* Querry for video format: */
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = cam->width;
	fmt.fmt.pix.height = cam->height;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
	fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

	REINTR(rv, v4l2_ioctl(priv->fd, VIDIOC_S_FMT, &fmt));
	if (rv < 0) {
		log("video format RGB24 is not supported");
		/* TODO: try another format??? */
		return -1;
	}

	/* Next lines had taken from capture.c. It is dark magic so we must not change them :) */
	/* Buggy driver paranoia. */
	min = fmt.fmt.pix.width * 2;
	if (fmt.fmt.pix.bytesperline < min)
		fmt.fmt.pix.bytesperline = min;
	min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
	if (fmt.fmt.pix.sizeimage < min)
		fmt.fmt.pix.sizeimage = min;

	cam->width = fmt.fmt.pix.width;
	cam->height = fmt.fmt.pix.height;
	priv->linebytes = fmt.fmt.pix.bytesperline;
	priv->img_len = fmt.fmt.pix.sizeimage;

	cam->image = calloc(cam->width * cam->height, sizeof(webcam_color_t));
	if (!cam->image) {
		log("not enought memory");
		return -1;
	}

	if (priv->io_method == IO_METHOD_MMAP) {
		rv = init_mmap(cam);
		if (rv < 0) {
			priv->io_method = IO_METHOD_READ;
			rv = init_read(cam);
		}
	} else
		rv = init_read(cam);

	if (rv) {
		log("can not init");
		free(cam->image);
		cam->image = NULL;
		return -1;
	}

	/* Ok then */
	return 0;
}

static int init_mmap(webcam_t *cam)
{
	struct v4l2_requestbuffers req;
	struct v4l2_buffer buf;
	int rv;
	priv_t *priv = cam->priv;
	unsigned i, j;

	memset(&req, 0, sizeof(req));
	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	REINTR(rv, v4l2_ioctl(priv->fd, VIDIOC_REQBUFS, &req));
	if (rv < 0) {
		if (errno == EINVAL)
			log("Memory mapping is not supported");
		else
			log("Can not init buffers");
		return -1;
	}

	if (req.count < 2) {
		log("Invalid buffers count");
		return -1;
	}

	priv->buffers = calloc(req.count, sizeof(struct buffer));
	if (!priv->buffers) {
		log("Not enought memory");
		return -1;
	}

	for (i = 0; i < req.count; i++) {
		memset(&buf, 0, sizeof(buf));

		buf.type = req.type;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		REINTR(rv, v4l2_ioctl(priv->fd, VIDIOC_QUERYBUF, &buf));
		if (rv == 0) {
			priv->buffers[i].len = buf.length;
			priv->buffers[i].start = v4l2_mmap(NULL, /* start anywhere */
					buf.length,
					PROT_READ | PROT_WRITE,
					MAP_SHARED,
					priv->fd, buf.m.offset);
			if (priv->buffers[i].start == MAP_FAILED)
				break;
		} else {
			break;
		}
	}

	if (i < req.count) {
		for (j = 0; j < i; j++) {
			v4l2_munmap(priv->buffers[j].start, priv->buffers[j].len);
		}

		free(priv->buffers);
		priv->buffers = NULL;

		log("mmap failed");

		return -1;
	}

	priv->buffers_count = req.count;

	return 0;
}

static int init_read(webcam_t *cam)
{
	priv_t *priv = cam->priv;

	priv->buffers_count = 0;
	priv->buffers = NULL;
	priv->buf = malloc(priv->img_len);
	if (!priv->buf) {
		log("not enought memory");
		return -1;
	}

	return 0;
}

static void process_image(webcam_t *cam, unsigned char *img, size_t img_len)
{
	unsigned x, y;
	priv_t *priv = cam->priv;

	for (y = 0; y < cam->height; y++)
		for (x = 0; x < cam->width; x++)
			cam->image[y * cam->width + x] =
				(img[y * priv->linebytes + x * 3] << 16) |
				(img[y * priv->linebytes + x * 3 + 1] << 8) |
				(img[y * priv->linebytes + x * 3 + 2]);
}

