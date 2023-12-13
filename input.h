#ifndef INPUT_H
#define INPUT_H

#include "arrivals.h"

// the time in seconds it takes for a car to cross the intersection
#define CROSS_TIME 5

// the array of arrivals for the intersection
const Arrival input_arrivals[] = {{0, NORTH, STRAIGHT, 0}, {1, SOUTH, LEFT, 1}, {2, EAST, STRAIGHT, 7}, {3, WEST, RIGHT, 13}};

#endif
