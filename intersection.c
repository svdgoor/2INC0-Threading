#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#include "arrivals.h"
#include "intersection_time.h"
#include "input.h"

/* 
 * curr_arrivals[][][]
 *
 * A 3D array that stores the arrivals that have occurred
 * The first two indices determine the entry lane: first index is Side, second index is Direction
 * curr_arrivals[s][d] returns an array of all arrivals for the entry lane on side s for direction d,
 *   ordered in the same order as they arrived
 */
static Arrival curr_arrivals[4][3][20];

/*
 * semaphores[][]
 *
 * A 2D array that defines a semaphore for each entry lane,
 *   which are used to signal the corresponding traffic light that a car has arrived
 * The two indices determine the entry lane: first index is Side, second index is Direction
 */
static sem_t semaphores[4][3];

/*
 * lights[]
 *
 * An array of structs that define the traffic lights
 */
static struct {Side side; Direction direction;} lights[9] =
{
  {NORTH, RIGHT},
  {NORTH, STRAIGHT},
  {EAST, RIGHT},
  {EAST, STRAIGHT},
  {EAST, LEFT},
  {SOUTH, STRAIGHT},
  {SOUTH, LEFT},
  {WEST, RIGHT},
  {WEST, LEFT}
};

/*
 * supply_arrivals()
 *
 * A function for supplying arrivals to the intersection
 * This should be executed by a separate thread
 */
static void* supply_arrivals()
{
  fprintf(stderr, "Arrival thread started\n");
  int t = 0;
  int num_curr_arrivals[4][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};

  // for every arrival in the list
  for (int i = 0; i < sizeof(input_arrivals)/sizeof(Arrival); i++)
  {
    // get the next arrival in the list
    Arrival arrival = input_arrivals[i];
    // wait until this arrival is supposed to arrive
    sleep(arrival.time - t);
    t = arrival.time;
    // store the new arrival in curr_arrivals
    curr_arrivals[arrival.side][arrival.direction][num_curr_arrivals[arrival.side][arrival.direction]] = arrival;
    num_curr_arrivals[arrival.side][arrival.direction] += 1;
    // increment the semaphore for the traffic light that the arrival is for
    sem_post(&semaphores[arrival.side][arrival.direction]);
  }

  return(0);
}

/*
 * print_traffic_light_change(Side side, Direction direction, bool green, int time, int for_car)
 * 
 * A function that traffic lights may use to output their state changes
 * Prints a formatted message to stdout
 * side: the side of the intersection that the traffic light is for
 * direction: the direction that the traffic light is for
 * green: whether the traffic light is green (true) or red (false)
 * time: the time at which the traffic light changes. Not used when not green
 */
static void* print_traffic_light_change(Side side, Direction direction, bool green, int time, int for_car)
{
  if (green)
  {
    fprintf(stdout, "traffic light %d %d turns green at time %d for car %d\n", side, direction, time, for_car);
  }
  else
  {
    fprintf(stdout, "traffic light %d %d turns red at time %d\n", side, direction, time);
  }
  return(0);
}

/*
 * all_cars_handled()
 *
 * Returns whether all arrivals have been handled based on the semaphores
 */
static bool all_cars_handled()
{
  bool all_handled = true;
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      int sem_value;
      sem_getvalue(&semaphores[i][j], &sem_value);
      if (sem_value > 0)
      {
        all_handled = false;
      }
    }
  }
  return all_handled;
}

/*
 * manage_light(void* arg)
 *
 * A function that implements the behaviour of a traffic light
 * As argument receives the index of the traffic light in the lights array
 * While not all arrivals have been handled, repeatedly:
 * - waits for an arrival using the semaphore for this traffic light
 * - locks the right mutex(es)
 * - makes the traffic light turn green
 * - sleeps for CROSS_TIME seconds
 * - makes the traffic light turn red
 * - unlocks the right mutex(es)
 */
static void* manage_light(void* arg)
{
  int light_index = (int)arg;
  Side side = lights[light_index].side;
  Direction direction = lights[light_index].direction;
  fprintf(stderr, "Light %d / %d started\n", side, direction);

  // keep track of how many cars have passed
  int cars_passed = 0;

  // work until controller kills the thread
  while (true)
  {
    // wait for an arrival
    sem_wait(&semaphores[side][direction]);
    // lock the right mutex(es)
    // TODO
    // make the traffic light turn green
    print_traffic_light_change(side, direction, true, get_time_passed(), curr_arrivals[side][direction][cars_passed].id);
    cars_passed += 1;
    // sleep for CROSS_TIME seconds
    sleep(CROSS_TIME);
    // make the traffic light turn red
    print_traffic_light_change(side, direction, false, get_time_passed(), 0); 
    // unlock the right mutex(es)
    // TODO
  }

  return(0);
}


int main(int argc, char * argv[])
{
  // create semaphores to wait/signal for arrivals
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      sem_init(&semaphores[i][j], 0, 0);
    }
  }

  // create a thread per traffic light that executes manage_light
  pthread_t light_threads[8];
  fprintf(stderr, "Creating traffic light threads...\n");
  for (int i = 0; i < sizeof(lights)/sizeof(lights[0]); i++)
  {
    fprintf(stderr, "Light %d / %d created\n", i, lights[i].side, lights[i].direction);
    pthread_create(&light_threads[i], NULL, manage_light, (void*)i);
  }
  fprintf(stderr, "Traffic light threads created\n");

  // start the timer
  fprintf(stderr, "Starting timer...\n");
  start_time();
  fprintf(stderr, "Timer started\n");

  // create a thread that executes supply_arrivals
  pthread_t arrival_thread;
  fprintf(stderr, "Creating arrival thread...\n");
  pthread_create(&arrival_thread, NULL, supply_arrivals, NULL);
  fprintf(stderr, "Arrival thread created\n");

  // wait for all arrivals to finish
  fprintf(stderr, "Waiting for threads to finish...\n");
  pthread_join(arrival_thread, NULL);
  fprintf(stderr, "Arrival thread finished\n");

  // wait for all cars to be handled
  while (!all_cars_handled())
  {
    sleep(1);
  }
  fprintf(stderr, "All cars handled\n");

  // kill all traffic light threads
  fprintf(stderr, "Killing traffic light threads...\n");
  for (int i = 0; i < sizeof(lights)/sizeof(lights[0]); i++)
  {
    pthread_cancel(light_threads[i]);
  }
  fprintf(stderr, "Traffic light threads killed\n");

  // destroy semaphores
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      sem_destroy(&semaphores[i][j]);
    }
  }
}
