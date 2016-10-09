#ifndef SOURCE_H
#define SOURCE_H

#include <Xinput.h>
#include <stdlib.h>

#define PI 3.14159265
#define XRIGHT 32768
#define XLEFT -32768
#define YTOP 32768
#define YBOTTOM -32768

#define XDEADZONE 1000
#define YDEADZONE 1000

#define circle 8092
#define cross 4096
#define square 16384
#define triangle 32768

#define FPS 90.0

unsigned char flicker_frames = 30;
				
//i is start time, i+1 is length
float timings[] = { 5, 46, //flickering
					10, 41, //faster flickering
					23, 8.1, //IPD 1
					31, 8, //IPD 2
					39, 12, //IPD rand
					51, 40, //IPD crossed
					63, 17, //yaw and pitch
				 };

#define STARTTIME 40*FPS //timings[12]*FPS
#define DEBUGGING 1
#endif