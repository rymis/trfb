#include <trfb.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

ssize_t trfb_send_all(int sock, const void *buf, size_t len)
{
	ssize_t l;
	size_t pos = 0;
	const unsigned char *bufp = buf;

	while (pos < len) {
		l = write(sock, bufp + pos, len - pos);
		if (l < 0) {
			if (errno == EAGAIN || errno == EINTR) {
				continue;
			}
			trfb_msg("write failed: %s", strerror(errno));
			return -1;
		}

		if (l == 0) {
			trfb_msg("connection closed");
			return 0;
		}

		pos += l;
	}

	return len;
}

ssize_t trfb_recv_all(int sock, void *buf, size_t len)
{
	ssize_t l;
	size_t pos = 0;
	unsigned char *bufp = buf;

	while (pos < len) {
		l = read(sock, bufp + pos, len - pos);
		if (l < 0) {
			if (errno == EAGAIN || errno == EINTR) {
				continue;
			}
			trfb_msg("read failed: %s", strerror(errno));
			return -1;
		}

		if (l == 0) {
			trfb_msg("connection closed");
			return 0;
		}

		pos += l;
	}

	return len;
}

int trfb_msg_protocol_version_encode(trfb_msg_protocol_version_t *msg, unsigned char *buf, size_t *len)
{
	if (!buf) {
		*len = 12;
		return 0;
	}

	if (*len < 12) {
		*len = 12;
		return -1;
	}

	memcpy(buf, "RFB 003.008\n", 12);
	switch (msg->proto) {
		case trfb_v3:
			buf[10] = '3';
			break;
		case trfb_v7:
			buf[10] = '7';
			break;
		case trfb_v8:
			buf[10] = '8';
			break;
		default:
			buf[10] = '3';
			break;
	}
	*len = 12;

	return 0;
}

int trfb_msg_protocol_version_decode(trfb_msg_protocol_version_t *msg, const unsigned char *buf, size_t len)
{
	char mbuf[13];

	if (len != 12) {
		trfb_msg("Invalid ProtocolVersion message");
		return -1;
	}

	if (memcmp(buf, "RFB 003.00", 10) || buf[11] != '\n') {
		memcpy(mbuf, buf, 12);
		mbuf[12] = 0;
		trfb_msg("Invalid ProtocolVersion message. Using version 3. (%s)", mbuf);
		msg->proto = trfb_v3;
		return 0;
	}

	if (buf[10] == '8') {
		msg->proto = trfb_v8;
	} else if (buf[10] == '7') {
		msg->proto = trfb_v7;
	} else {
		msg->proto = trfb_v3;
	}

	return 0;
}

