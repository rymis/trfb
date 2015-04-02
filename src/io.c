#include <trfb.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static void sock_free(void *ctx);
static ssize_t sock_read(trfb_io_t *io, void *buf, ssize_t len, unsigned timeout);
static ssize_t sock_write(trfb_io_t *io, const void *buf, ssize_t len, unsigned timeout);
trfb_io_t* trfb_io_socket_wrap(int sock)
{
	trfb_io_t *io;

	io = calloc(1, sizeof(trfb_io_t));
	if (!io) {
		return NULL;
	}

	io->ctx = malloc(sizeof(int));
	if (!io->ctx) {
		free(io);
		return NULL;
	}

	*((int*)io->ctx) = sock;
	io->read = sock_read;
	io->write = sock_write;
	io->free = sock_free;
	io->error = 0;

	return io;
}

ssize_t trfb_io_read(trfb_io_t *io, void *buf, ssize_t len, unsigned timeout)
{
	ssize_t l;

	if (!io) {
		return -1;
	}

	if (!io->read || len < 0 || !buf) {
		io->error = EINVAL;
		return -1;
	}

	if (io->rpos >= io->rlen) {
		l = io->read(io, io->rbuf, TRFB_BUFSIZ, timeout);
		if (l <= 0)
			return l; /* We can not read data (error or timeout) */
		io->rpos = 0;
		io->rlen = l;
	}

	l = io->rlen - io->rpos; /* Maximum length to read without I/O */
	if (l > len)
		l = len;
	memcpy(buf, io->rbuf + io->rpos, l);
	io->rpos += l;

	if (io->rpos == io->rlen) { /* End of buffer */
		io->rpos = 0;
		io->rlen = 0;
	}

	return l;
}

ssize_t trfb_io_write(trfb_io_t *io, const void *buf, ssize_t len, unsigned timeout)
{
	const unsigned char *p = buf;
	ssize_t pos = 0;
	ssize_t l;

	if (!io) {
		return -1;
	}

	if (!buf || len < 0 || !io->write) {
		io->error = EINVAL;
		return -1;
	}

	if (io->wlen < TRFB_BUFSIZ) { /* We have got some place here */
		pos = TRFB_BUFSIZ - io->wlen; /* free space */
		if (pos > len)
			pos = len;
		memcpy(io->wbuf + io->wlen, buf, pos);
		io->wlen += pos;

		if (pos == len) { /* We have written all the data! */
			return len;
		}
	}

	if (trfb_io_flush(io, timeout) < 0) { /* It is error! */
		return -1;
	}

	if (io->wlen < TRFB_BUFSIZ) { /* We could write some data! */
		l = TRFB_BUFSIZ - io->wlen;
		if (l > len - pos)
			l = len - pos;
		memcpy(io->wbuf + io->wlen, p + pos, l);
		io->wlen += l;
		pos += l;
	}

	return pos;
}

void trfb_io_free(trfb_io_t *io)
{
	if (io) {
		if (io->free)
			io->free(io->ctx);
		memset(io, 0, sizeof(trfb_io_t));
		free(io);
	}
}

static void sock_free(void *ctx)
{
	int *sock = ctx;

	if (sock)
		close(*sock);

	free(ctx);
}

static ssize_t sock_read(trfb_io_t *io, void *buf, ssize_t len, unsigned timeout)
{
	int rv;
	fd_set fds;
	int *sock;
	struct timeval tv;
	ssize_t sz;

	if (!io->ctx) {
		io->error = EINVAL;
		return -1;
	}

	sock = io->ctx;

	do {
		FD_ZERO(&fds);
		FD_SET(*sock, &fds);

		if (timeout) {
			tv.tv_sec = timeout / 1000;
			tv.tv_usec = 1000 * (timeout % 1000);

			rv = select(*sock + 1, &fds, NULL, NULL, &tv);
		} else {
			rv = select(*sock + 1, &fds, NULL, NULL, NULL);
		}

		if (rv < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				io->error = errno;
				return -1;
			}
		}
	} while (rv < 0);

	if (rv == 0) {
		return 0;
	}

	for (;;) {
		sz = recv(*sock, buf, len, 0);
		if (sz < 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			else {
				io->error = errno;
				return -1;
			}
		} else if (sz == 0) {
			io->error = EIO;
			return -1;
		}

		return sz;
	}

	return -1; /* not reached */
}

static ssize_t sock_write(trfb_io_t *io, const void *buf, ssize_t len, unsigned timeout)
{
	int rv;
	fd_set fds;
	int *sock;
	struct timeval tv;
	ssize_t sz;

	if (!io->ctx) {
		io->error = EINVAL;
		return -1;
	}

	sock = io->ctx;

	do {
		FD_ZERO(&fds);
		FD_SET(*sock, &fds);

		if (timeout) {
			tv.tv_sec = timeout / 1000;
			tv.tv_usec = 1000 * (timeout % 1000);

			rv = select(*sock + 1, NULL, &fds, NULL, &tv);
		} else {
			rv = select(*sock + 1, NULL, &fds, NULL, NULL);
		}

		if (rv < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				io->error = errno;
				return -1;
			}
		}
	} while (rv < 0);

	if (rv == 0) {
		return 0;
	}

	for (;;) {
		sz = send(*sock, buf, len, 0);
		if (sz < 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			else {
				io->error = errno;
				return -1;
			}
		} else if (sz == 0) {
			io->error = EIO;
			return -1;
		}

		return sz;
	}

	return -1; /* not reached */
}

int trfb_io_flush(trfb_io_t *io, unsigned timeout)
{
	ssize_t r;

	if (io->wlen == 0)
		return 0;

	r = io->write(io, io->wbuf, io->wlen, timeout);
	if (r < 0) { /* It is error */
		return -1;
	} else if (r == 0) {
		return 0; /* It is timeout */
	} else {
		if ((size_t)r < io->wlen) {
			memmove(io->wbuf + r, io->wbuf, io->wlen - r); /* It is slow but I can't find better way */
			io->wlen -= r;
		} else {
			io->wlen = 0;
		}
	}

	return io->wlen; /* bytes remaining */
}

int trfb_io_fgetc(trfb_io_t *io, unsigned timeout)
{
	unsigned char c;
	ssize_t r;

	r = trfb_io_read(io, &c, 1, timeout);
	if (r == 1) {
		return c;
	}

	return TRFB_EOF;
}

int trfb_io_fputc(unsigned char c, trfb_io_t *io, unsigned timeout)
{
	if (trfb_io_write(io, &c, 1, timeout) == 1)
		return 1;
	return TRFB_EOF;
}


