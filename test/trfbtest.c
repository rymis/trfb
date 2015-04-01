#include <trfb.h>
#include <c11threads.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

int split_words(char *s, int *argc, const char **argv)
{
	int l = strlen(s);
	int arglen = *argc;
	int i = 0;
	int acnt = 0;

	for (;;) {
		while (s[i] && isspace(s[i]))
			i++;

		if (!s[i])
			break;

		if (acnt >= arglen) {
			fprintf(stderr, "Error: too much arguments!\n");
			return -1;
		}

		argv[acnt++] = s + i;
		while (s[i] && !isspace(s[i]))
			i++;
		
		if (!s[i]) {
			break;
		} else {
			s[i] = 0;
			++i;
		}
	}

	*argc = acnt;

	return 0;
}

int main(int argc, char *argv[])
{
	char buf[512];
	trfb_server_t *srv;
	int ac;
	const char* av[32];
	unsigned i, j, di = 0;

	srv = trfb_server_create(640, 480);
	if (!srv) {
		fprintf(stderr, "Error: can't create server!\n");
		return 1;
	}

	if (trfb_server_bind(srv, "localhost", "5913")) {
		fprintf(stderr, "Error: can't bind!\n");
		return 1;
	}

	if (trfb_server_start(srv)) {
		fprintf(stderr, "Error: can't start server!\n");
		return 1;
	}

	for (;;) {
		for (i = 0; i < 256; i++) {
			for (j = 0; j < 256; j++) {
				trfb_framebuffer_set_pixel(srv->fb, (i + di) % 256, j, TRFB_RGB(i, j, 100));
			}
		}
		di = (di + 10) % 256;

		if (!fgets(buf, sizeof(buf), stdin)) {
			strcpy(buf, "exit");
		}

		ac = sizeof(av) / sizeof(av[0]);
		if (split_words(buf, &ac, av)) {
			fprintf(stderr, "Error: can't parse commands\n");
		}

		if (ac == 0)
			continue;

		if (!strcmp(av[0], "exit") || !strcmp(av[0], "quit")) {
			/* EXIT! */
			trfb_server_stop(srv);
			trfb_server_destroy(srv);
			exit(0);
		} else {
			int i;
			printf("%s:\n", av[0]);
			for (i = 1; i < ac; i++)
				printf("\t%s\n", av[i]);
		}
	}

	return 0;
}
