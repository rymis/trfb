#ifndef LIBWEBCAM_H_INC
#define LIBWEBCAM_H_INC

#include <stdlib.h>

#ifdef WEBCAM_COLOR_T
/* User-defined type for color. It MUST be of size = 4 */
typedef WEBCAM_COLOR_T webcam_color_t;
#else
# ifdef WIN32
#  include <windows.h>
typedef DWORD webcam_color_t;
# else
#  include <stdint.h>
typedef uint32_t webcam_color_t;
# endif
#endif

#ifdef __cplusplus
extern "C" {
#endif /* } */

/* Here you can see only must frequently used controls */
typedef enum webcam_controls {
	WEBCAM_BRIGHTNESS,
	WEBCAM_CONTRAST,
	WEBCAM_SATURATION,
	WEBCAM_GAMMA
} webcam_controls_t;

typedef struct webcam {
	void *priv;

	/* Name of the camera. */
	char *name;

	/* Width and height of the image: */
	unsigned width, height;

	/* Image data */
	webcam_color_t *image;

	/* img contains uint32_t's in form 0x00rrggbb */
} webcam_t;

/* List all cameras connected */
int webcam_list(int *ids, unsigned *cnt);

/* Get the name of camera with number =num. You must free name with free. */
char* webcam_name(int id);

/* Try to open camera with number =num. Width and height is recomended values so you must look inside webcam_t for actual sizes. */
webcam_t* webcam_open(int id, unsigned width, unsigned height);

/* Close device and free resources */
void webcam_close(webcam_t *cam);

/* Enable capturing: */
int webcam_start(webcam_t *cam);

/* Disable capturing: */
int webcam_stop(webcam_t *cam);

/* Wait for next frame for maximum =delay ms (0 = forever) */
int webcam_wait_frame(webcam_t *cam, unsigned delay);

/* Set control to camera. Value must be in interval [0-100] */
int webcam_set_control(webcam_t *cam, webcam_controls_t id, int value);
/* Get control from camera. Returns value in [0-100] */
int webcam_get_control(webcam_t *cam, webcam_controls_t id);

/* extern "C" { */
#ifdef __cplusplus
}
#endif

#endif

