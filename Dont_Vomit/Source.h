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

#define NUM_EFFECTS 8
				
//i is start time, i+1 is length
float timings[] = { 5, 66, //flickering
					11, 60, //faster flickering
					23, 16.1, //IPD 1
					39, 16, //IPD 2
					55, 16, //IPD rand
					71, INFINITY, //IPD crossed
					83, 29, //yaw and pitch
					100, INFINITY, //latency
				 };
//if an effect is enabled
bool effects_enabled[NUM_EFFECTS];

#define STARTTIME 0
#define DEBUGGING 1
#endif