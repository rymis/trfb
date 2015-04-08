/* Load libv4l2.so dynamically */
#if !defined(HAVE_LIBV4L2)

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

static int (*x_open)() = NULL;
static int (*x_close)() = NULL;
static int (*x_ioctl)() = NULL;
static int (*x_read)() = NULL;
static void* (*x_mmap)() = NULL;
static int (*x_munmap)() = NULL;
static int (*x_set_control)() = NULL;
static int (*x_get_control)() = NULL;

static const char* names[] = {
	"libv4l2.so",
	"libv4l2.so.0",
	"/usr/lib/libv4l2.so",
	"/usr/lib/libv4l2.so.0",
	"/usr/lib/libv4l/libv4l2.so",
	"/usr/lib/libv4l/libv4l2.so.0",
	NULL
};

static int set_control()
{
	return 0;
}

static int get_control()
{
	return -1;
}

static void init(void)
{
	static int initialized = 0;
	void *dll;
	int i;

	if (!initialized) {
		for (i = 0; names[i]; i++) {
			dll = dlopen(names[i], RTLD_LOCAL | RTLD_NOW);
			if (dll)
				break;
		}

		if (!dll) {
			fprintf(stderr, "WARNING: can't load libv4l2.so\n");
		}

#define X(nm) \
		do { \
			x_##nm = dll? dlsym(dll, "v4l2_" #nm): NULL; \
			if (!x_##nm) { \
				fprintf(stderr, "WARNING: using %s not from v4l2!\n", #nm); \
				x_##nm = nm; \
			} \
		} while (0)

		X(open);
		X(close);
		X(ioctl);
		X(read);
		X(mmap);
		X(munmap);
		X(get_control);
		X(set_control);
	}
}

int v4l2_open(const char *name, int flags, mode_t mode)
{
	init();
	return x_open(name, flags, mode);
}

int v4l2_close(int fd)
{
	init();
	return x_close(fd);
}

int v4l2_ioctl(int fd, int id, void *param)
{
	init();
	return x_ioctl(fd, id, param);
}

void* v4l2_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	init();
	return x_mmap(addr, length, prot, flags, fd, offset);
}

int v4l2_munmap(void *addr, size_t length)
{
	init();
	return x_munmap(addr, length);
}

int v4l2_set_control(int fd, int id, int val)
{
	init();
	return x_set_control(fd, id, val);
}

int v4l2_get_control(int fd, int id)
{
	init();
	return x_get_control(fd, id);
}

ssize_t v4l2_read(int fd, void *buf, size_t sz)
{
	init();
	return x_read(fd, buf, sz);
}

#endif /* !defined HAVE_LIBV4L2 */
