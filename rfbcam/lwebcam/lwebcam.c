/*
 *  This code based on capture.c from v4l project.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "lwebcam.h"

#define CLEAR(x) memset (&(x), 0, sizeof (x))

struct webcam_image_buffer {
        void *                  start;
        size_t                  length;
};

/*
static char *           dev_name        = NULL;
static io_method	io		= IO_METHOD_MMAP;
static int              fd              = -1;
struct buffer *         buffers         = NULL;
static unsigned int     n_buffers       = 0;

*/

static void
errno_exit                      (const char *           s)
{
        fprintf (stderr, "%s error %d, %s\n",
                 s, errno, strerror (errno));

        exit (EXIT_FAILURE);
}

static int xioctl(int fd, int request, void* arg)
{
        int r;

        do {
		r = ioctl (fd, request, arg);
	} while (r == -1 && errno == EINTR);

        return r;
}

/* Warning: process image must return -1 on failure and 1 on success */
static int process_image(struct webcam* cam, unsigned char *p, int len)
{
	if (v4lconvert_convert(cam->v4lconvert_data,
				cam->src_fmt,
				cam->fmt,
				p, len,
				cam->img_data, cam->img_data_len) < 0) {
		fprintf(stderr, "Error: can not convert image\n");
		return -1;
	}

	return 1;
}

static int read_frame(struct webcam *cam)
{
        struct v4l2_buffer buf;
	unsigned int i;
	int l;
	int rv;

	switch (cam->io) {
	case WEBCAM_IO_METHOD_READ:
    		if ((l = read(cam->fd, cam->buffers[0].start, cam->buffers[0].length)) < 0) {
            		switch (errno) {
            		case EAGAIN:
                    		return 0;

			case EIO:
				/* Could ignore EIO, see spec. */

				/* fall through */

			default:
				return -1;
			}
		}

    		rv = process_image(cam, cam->buffers[0].start, l);

		break;

	case WEBCAM_IO_METHOD_MMAP:
		CLEAR(buf);

            	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            	buf.memory = V4L2_MEMORY_MMAP;

    		if (xioctl(cam->fd, VIDIOC_DQBUF, &buf) == -1) {
            		switch (errno) {
            		case EAGAIN:
                    		return 0;

			case EIO:
				/* Could ignore EIO, see spec. */

				/* fall through */

			default:
				/* TODO: we must no exit, but let stop capturing... */
				return -1;
			}
		}

		/* TODO: not assert, but check and make more buffers */
                assert(buf.index < cam->n_buffers);

	        rv = process_image(cam, cam->buffers[buf.index].start, buf.bytesused);

		if (xioctl(cam->fd, VIDIOC_QBUF, &buf) == -1)
			return -1;

		break;

	case WEBCAM_IO_METHOD_USERPTR:
		CLEAR(buf);

    		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    		buf.memory = V4L2_MEMORY_USERPTR;

		if (xioctl(cam->fd, VIDIOC_DQBUF, &buf) == -1) {
			switch (errno) {
			case EAGAIN:
				return 0;

			case EIO:
				/* Could ignore EIO, see spec. */

				/* fall through */

			default:
				return -1;
			}
		}

		for (i = 0; i < cam->n_buffers; ++i)
			if (buf.m.userptr == (unsigned long)cam->buffers[i].start
			    && buf.length == cam->buffers[i].length)
				break;

		assert(i < cam->n_buffers); /* TODO: !assert */

    		rv = process_image(cam, (unsigned char*)buf.m.userptr, buf.bytesused);

		if (xioctl(cam->fd, VIDIOC_QBUF, &buf) == -1)
			return -1;

		break;
	}

	return rv;
}

static int stop_capturing(struct webcam *cam)
{
        enum v4l2_buf_type type;

	switch (cam->io) {
	case WEBCAM_IO_METHOD_READ:
		/* Nothing to do. */
		break;

	case WEBCAM_IO_METHOD_MMAP:
	case WEBCAM_IO_METHOD_USERPTR:
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (xioctl (cam->fd, VIDIOC_STREAMOFF, &type) < 0)
			return -1;

		break;
	}

	return 0;
}

static int start_capturing(struct webcam *cam)
{
        unsigned int i;
        enum v4l2_buf_type type;

	switch (cam->io) {
	case WEBCAM_IO_METHOD_READ:
		/* Nothing to do. */
		break;

	case WEBCAM_IO_METHOD_MMAP:
		for (i = 0; i < cam->n_buffers; ++i) {
            		struct v4l2_buffer buf;

        		CLEAR (buf);

        		buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        		buf.memory      = V4L2_MEMORY_MMAP;
        		buf.index       = i;

        		if (xioctl (cam->fd, VIDIOC_QBUF, &buf) < 0)
                    		return -1;
		}
		
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (xioctl(cam->fd, VIDIOC_STREAMON, &type) < 0)
			return -1;

		break;

	case WEBCAM_IO_METHOD_USERPTR:
		for (i = 0; i < cam->n_buffers; ++i) {
            		struct v4l2_buffer buf;

        		CLEAR (buf);

        		buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        		buf.memory      = V4L2_MEMORY_USERPTR;
			buf.index       = i;
			buf.m.userptr	= (unsigned long) cam->buffers[i].start;
			buf.length      = cam->buffers[i].length;

			if (-1 == xioctl(cam->fd, VIDIOC_QBUF, &buf))
                    		return -1;
		}

		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (xioctl(cam->fd, VIDIOC_STREAMON, &type) < 0)
			return -1;

		break;
	}

	return 0;
}

static int uninit_device(struct webcam *cam)
{
        unsigned int i;

	switch (cam->io) {
	case WEBCAM_IO_METHOD_READ:
		free(cam->buffers[0].start);
		break;

	case WEBCAM_IO_METHOD_MMAP:
		for (i = 0; i < cam->n_buffers; ++i)
			if (munmap(cam->buffers[i].start, cam->buffers[i].length) < 0)
				return -1;
		break;

	case WEBCAM_IO_METHOD_USERPTR:
		for (i = 0; i < cam->n_buffers; ++i)
			free(cam->buffers[i].start);
		break;
	}

	free(cam->buffers);

	return 0;
}

static void init_read(struct webcam* cam, unsigned int buffer_size)
{
        cam->buffers = calloc(1, sizeof(*cam->buffers));

        if (!cam->buffers) {
                fprintf (stderr, "Out of memory\n");
                exit(EXIT_FAILURE); /* TODO! */
        }

	cam->buffers[0].length = buffer_size;
	cam->buffers[0].start = malloc(buffer_size);

	if (!cam->buffers[0].start) {
    		fprintf (stderr, "Out of memory\n");
            	exit (EXIT_FAILURE);
	}
}

static void init_mmap(struct webcam *cam)
{
	struct v4l2_requestbuffers req;

        CLEAR (req);

        req.count               = 4;
        req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory              = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(cam->fd, VIDIOC_REQBUFS, &req)) {
                if (EINVAL == errno) {
                        fprintf (stderr, "device does not support "
                                 "memory mapping\n");
                        exit (EXIT_FAILURE); /* TODO: */
                } else {
                        errno_exit ("VIDIOC_REQBUFS"); /* TODO: */
                }
        }

        if (req.count < 2) {
                fprintf (stderr, "Insufficient buffer memory on device\n");
                exit (EXIT_FAILURE); /* TODO! */
        }

        cam->buffers = calloc(req.count, sizeof (*cam->buffers));

        if (!cam->buffers) {
                fprintf (stderr, "Out of memory\n");
                exit (EXIT_FAILURE); /* TODO */
        }

        for (cam->n_buffers = 0; cam->n_buffers < req.count; ++cam->n_buffers) {
                struct v4l2_buffer buf;

                CLEAR (buf);

                buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory      = V4L2_MEMORY_MMAP;
                buf.index       = cam->n_buffers;

                if (-1 == xioctl(cam->fd, VIDIOC_QUERYBUF, &buf))
                        errno_exit ("VIDIOC_QUERYBUF"); /* TODO: */

                cam->buffers[cam->n_buffers].length = buf.length;
                cam->buffers[cam->n_buffers].start =
                        mmap (NULL /* start anywhere */,
                              buf.length,
                              PROT_READ | PROT_WRITE /* required */,
                              MAP_SHARED /* recommended */,
                              cam->fd, buf.m.offset);

                if (MAP_FAILED == cam->buffers[cam->n_buffers].start)
                        errno_exit ("mmap"); /* TODO */
        }
}

static void init_userp(struct webcam *cam, unsigned int buffer_size)
{
	struct v4l2_requestbuffers req;
        unsigned int page_size;

        page_size = getpagesize ();
        buffer_size = (buffer_size + page_size - 1) & ~(page_size - 1);

        CLEAR (req);

        req.count               = 4;
        req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory              = V4L2_MEMORY_USERPTR;

        if (-1 == xioctl (cam->fd, VIDIOC_REQBUFS, &req)) {
                if (EINVAL == errno) {
                        fprintf (stderr, "device does not support "
                                 "user pointer i/o\n");
                        exit (EXIT_FAILURE); /* TODO */
                } else {
                        errno_exit ("VIDIOC_REQBUFS");
                }
        }

        cam->buffers = calloc (4, sizeof (*cam->buffers));

        if (!cam->buffers) {
                fprintf (stderr, "Out of memory\n");
                exit (EXIT_FAILURE); /* TODO */
        }

        for (cam->n_buffers = 0; cam->n_buffers < 4; ++cam->n_buffers) {
                cam->buffers[cam->n_buffers].length = buffer_size;
                cam->buffers[cam->n_buffers].start = memalign (/* boundary */ page_size,
                                                     buffer_size);

                if (!cam->buffers[cam->n_buffers].start) {
    			fprintf (stderr, "Out of memory\n");
            		exit (EXIT_FAILURE); /* TODO: */
		}
        }
}

static int init_device(struct webcam* cam)
{
        struct v4l2_capability cap;
        struct v4l2_cropcap cropcap;
        struct v4l2_crop crop;
	unsigned int min;

        if (-1 == xioctl (cam->fd, VIDIOC_QUERYCAP, &cap)) {
                if (EINVAL == errno) {
                        fprintf (stderr, "device is no V4L2 device\n");
                        return -1;
                } else {
                        return -1;
                }
        }

        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
                fprintf (stderr, "device is no video capture device\n");
                return -1;
        }

	switch (cam->io) {
	case WEBCAM_IO_METHOD_READ:
		if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
			fprintf (stderr, "device does not support read i/o\n");
			return -1;
		}

		break;

	case WEBCAM_IO_METHOD_MMAP:
	case WEBCAM_IO_METHOD_USERPTR:
		if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
			fprintf (stderr, "device does not support streaming i/o\n");
			return -1;
		}

		break;
	}

        /* Select video input, video standard and tune here. */
	CLEAR(cropcap);

        cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (0 == xioctl (cam->fd, VIDIOC_CROPCAP, &cropcap)) {
                crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                crop.c = cropcap.defrect; /* reset to default */

                if (-1 == xioctl (cam->fd, VIDIOC_S_CROP, &crop)) {
                        switch (errno) {
                        case EINVAL:
                                /* Cropping not supported. */
                                break;
                        default:
                                /* Errors ignored. */
                                break;
                        }
                }
        } else {	
                /* Errors ignored. */
        }


        CLEAR(*cam->fmt);

        cam->fmt->type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cam->fmt->fmt.pix.width       = cam->width; 
        cam->fmt->fmt.pix.height      = cam->height;
        cam->fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
        cam->fmt->fmt.pix.field       = V4L2_FIELD_INTERLACED;

        /* Note VIDIOC_S_FMT may change width and height. */
	//if (cam->width != fmt.fmt.pix.width || cam->height != fmt.fmt.pix.height) {
	//	fprintf(stderr, "Warning: image format was changed t %dx%d\n", fmt.fmt.pix.width, fmt.fmt.pix.height);
	//	cam->width = fmt.fmt.pix.width;
	//	cam->height = fmt.fmt.pix.height;
	//}

	cam->v4lconvert_data = v4lconvert_create(cam->fd);
	if (!cam->v4lconvert_data) {
		fprintf(stderr, "Error: can not create converter\n");
		return -1;
	}

	if (v4lconvert_try_format(cam->v4lconvert_data, cam->fmt, cam->src_fmt) != 0) {
		fprintf(stderr, "Error: no converter available\n");
		return -1;
	}

        if (-1 == xioctl (cam->fd, VIDIOC_S_FMT, cam->src_fmt)) {
		fprintf(stderr, "Error: source format error\n");
                return -1;
	}

	cam->img_data_len = cam->fmt->fmt.pix.sizeimage;
	cam->img_data = malloc(cam->img_data_len);
	if (!cam->img_data) {
		return -1;
	}

	switch (cam->io) {
	case WEBCAM_IO_METHOD_READ:
		init_read(cam, cam->src_fmt->fmt.pix.sizeimage);
		break;

	case WEBCAM_IO_METHOD_MMAP:
		init_mmap(cam);
		break;

	case WEBCAM_IO_METHOD_USERPTR:
		init_userp(cam, cam->src_fmt->fmt.pix.sizeimage);
		break;
	}

	return 0;
}

/****************************************************************************/
/* API                                                                      */
/****************************************************************************/
struct webcam* webcam_open(const char *dev, webcam_io_method_t io_method, int w, int h)
{
	struct webcam* r = NULL;
        struct stat st; 

	if (w < 0) { /* TODO: auto */
		w = 640;
		h = 480;
	}

	r = (struct webcam*)malloc(sizeof(struct webcam));
	if (!r) {
		return NULL;
	}

	memset(r, 0, sizeof(struct webcam));

	if (stat(dev, &st) < 0) {
		free(r);
		return NULL;
	}

        if (!S_ISCHR(st.st_mode)) {
                fprintf(stderr, "%s is no device\n", dev);
		free(r);
		return NULL;
        }

        r->fd = open(dev, O_RDWR /* required */ | O_NONBLOCK, 0);
	if (r->fd < 0) {
		free(r);
                fprintf(stderr, "can not open %s\n", dev);
		return NULL;
	}

	r->io = io_method;
	r->width = w;
	r->height = h;
	r->fmt = (struct v4l2_format*)malloc(sizeof(struct v4l2_format));
	r->src_fmt = (struct v4l2_format*)malloc(sizeof(struct v4l2_format));
	if (!r->fmt || !r->src_fmt) {
		if (r->fmt) free(r->fmt);
		if (r->src_fmt) free(r->src_fmt);
		close(r->fd);
		free(r);
		fprintf(stderr, "Not enought memory\n");
		return NULL;
	}

	if (io_method == WEBCAM_IO_METHOD_AUTO) {
		/* Try to initialize device with mmap io-method */
		r->io = WEBCAM_IO_METHOD_MMAP;
		if (init_device(r) == 0)
			return r;
	}

	/* r->io = WEBCAM_IO_METHOD_READ; */
	if (init_device(r) < 0) {
		close(r->fd);
		if (r->fmt) free(r->fmt);
		if (r->src_fmt) free(r->src_fmt);
		if (r->img_data) free(r->img_data);
		free(r);
		fprintf(stderr, "Error: initialization failed\n");
		return NULL;
	}
	
	return r;
}

int webcam_start_capturing(struct webcam* cam)
{
	return start_capturing(cam);
}

int webcam_wait_frame(struct webcam *cam, int secs)
{
	fd_set fds;
	struct timeval tv;
	int r;

	for (;;) {
		FD_ZERO(&fds);
		FD_SET(cam->fd, &fds);

		/* Timeout. */
		tv.tv_sec = secs;
		tv.tv_usec = 0;

		r = select(cam->fd + 1, &fds, NULL, NULL, &tv);

		if (r < 0) {
			if (errno == EINTR) {
				fprintf(stderr, "Warning: EINTR\n");
				continue;
			}

			return -1;
		}

		if (r == 0) {
			fprintf (stderr, "select timeout\n");
			return 0;
		}

		r = read_frame(cam);
		if (r < 0) {
			return -1;
		}

		if (r > 0)
			break;
	}

	return 1;
}

int webcam_stop_capturing(struct webcam *cam)
{
	return stop_capturing(cam);
}

int webcam_close(struct webcam *cam)
{
	int r = 0;

	if (uninit_device(cam) < 0)
		r = -1;

	if (close(cam->fd) < 0)
		r = -1;

	if (cam->fmt) free(cam->fmt);
	if (cam->src_fmt) free(cam->src_fmt);
	if (cam->img_data) free(cam->img_data);


	free(cam);

	return r;
}

#if 0
static const char short_options [] = "d:hmru";

static const struct option
long_options [] = {
        { "device",     required_argument,      NULL,           'd' },
        { "help",       no_argument,            NULL,           'h' },
        { "mmap",       no_argument,            NULL,           'm' },
        { "read",       no_argument,            NULL,           'r' },
        { "userp",      no_argument,            NULL,           'u' },
        { 0, 0, 0, 0 }
};

int
main                            (int                    argc,
                                 char **                argv)
{
        dev_name = "/dev/video";

        for (;;) {
                int index;
                int c;
                
                c = getopt_long (argc, argv,
                                 short_options, long_options,
                                 &index);

                if (-1 == c)
                        break;

                switch (c) {
                case 0: /* getopt_long() flag */
                        break;

                case 'd':
                        dev_name = optarg;
                        break;

                case 'h':
                        usage (stdout, argc, argv);
                        exit (EXIT_SUCCESS);

                case 'm':
                        io = IO_METHOD_MMAP;
			break;

                case 'r':
                        io = IO_METHOD_READ;
			break;

                case 'u':
                        io = IO_METHOD_USERPTR;
			break;

                default:
                        usage (stderr, argc, argv);
                        exit (EXIT_FAILURE);
                }
        }

        open_device ();

        init_device ();

        start_capturing ();

        mainloop ();

        stop_capturing ();

        uninit_device ();

        close_device ();

        exit (EXIT_SUCCESS);

        return 0;
}

#endif

