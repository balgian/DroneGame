// macros.h
#ifndef MACROS_H
#define MACROS_H

/*
* - Keyboard input manager (write to Blackboard -> 1 pipe)
* - Obstacle generator (read & write -> 2 pipes)
* - Target generators (read & write -> 2 pipes)
* - Drone dynamics process (read & write -> 2 pipes)
*
* Total: 7 pipes
*/
#define NUM_CHILD_PIPES 4
#define NUM_CHILD_PROCESSES 6

#define FRAME_RATE 70.0

#endif // MACROS_H