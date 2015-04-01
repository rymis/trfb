#ifndef WEBCAM_H_INC
#define WEBCAM_H_INC

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <asm/types.h>          /* for videodev2.h */
#include <linux/videodev2.h>
#include <libv4lconvert.h>

#ifdef __cplusplus
extern "C" {
#endif /* } */

typedef enum webcam_io_method {
	WEBCAM_IO_METHOD_READ,
	WEBCAM_IO_METHOD_MMAP,
	WEBCAM_IO_METHOD_USERPTR,
	WEBCAM_IO_METHOD_AUTO
} webcam_io_method_t;

struct webcam {
	int fd;

	webcam_io_method_t io;

	struct webcam_image_buffer *buffers;
	int n_buffers;

	unsigned char *img_data;
	int img_data_len;

	int width, height;

	struct v4lconvert_data *v4lconvert_data;
	struct v4l2_format *fmt, *src_fmt;
};

struct webcam* webcam_open(const char *dev, webcam_io_method_t mode, int w, int h); /* Open device */
int webcam_close(struct webcam* wc); /* Close device */
int webcam_wait_frame(struct webcam* wc, int secs); /* Wait for next frame, maximum secs seconds */
int webcam_start_capturing(struct webcam *wc); /* Start capturing */
int webcam_stop_capturing(struct webcam *wc); /* Stop capturing */

#ifdef __cplusplus
}
#endif

#endif

