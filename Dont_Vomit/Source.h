#ifndef SOURCE_H
#define SOURCE_H

#include <Xinput.h>
#include <stdlib.h>

#define PI 3.14159265
#define XRIGHT 32768
#define XLEFT -32768
#define YTOP 32768
#define YBOTTOM -32768

#define XDEADZONE 600
#define YDEADZONE 600

#define circle 8092
#define cross 4096
#define square 16384
#define triangle 32768

#define FPS 90.0

unsigned char flicker_frames = 30;
				
float timings[] = { 5, 78, 10, 78, 18, 24.1, 24, 36.1, 36, 55, 55, 145 };

#endif