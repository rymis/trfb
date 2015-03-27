#include <trfb.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void log_stderr(const char *message)
{
	if (message[0] == 'W' && message[1] == ':') {
		fprintf(stderr, "WARNING: %s\n", message + 2);
	} else if (message[0] == 'I' && message[1] == ':') {
		fprintf(stderr, "INFO: %s\n", message + 2);
	} else if (message[0] == 'E' && message[1] == ':') {
		fprintf(stderr, "ERROR: %s\n", message + 2);
	} else {
		fprintf(stderr, "ERROR: %s\n", message);
	}
}

void (*trfb_log_cb)(const char *message) = log_stderr;

/* Internal error message writing function: */
void trfb_msg(const char *fmt, ...)
{
	char buf[512];
	int i;
	va_list args;

	if (!trfb_log_cb)
		return;

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	buf[511] = 0; /* Paranoid */

	for (i = 0; buf[i]; i++) {
		if (buf[i] < ' ')
			buf[i] = ' ';
	}

	trfb_log_cb(buf);
}


