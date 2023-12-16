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
 * mutexes[]
 * 
 * An array of mutexes that are used to lock the intersection
 * The mutexes are used to ensure that only one car can be in the intersection at a time
 */
static pthread_mutex_t mutexes[9];

/*
 * mutex lock
 *
 * A mutex that is locked when a light is claiming certain parts of the intersection
 * This is used to ensure that only one light can claim mutexes at the same time,
 * to prevent deadlocks
 */
static pthread_mutex_t mutex_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * lights[]
 *
 * An array of structs that define the traffic lights
 */
static struct {Side side; Direction direction; int m1; int m2; int m3; int m4;} lights[9] =
{
  {NORTH, RIGHT, 1, 0, 0, 0},
  {NORTH, STRAIGHT, 2, 8, 9, 0},
  {EAST, RIGHT, 3, 0, 0, 0},
  {EAST, STRAIGHT, 1, 2, 4, 0},
  {EAST, LEFT, 5, 7, 9, 0},
  {SOUTH, STRAIGHT, 3, 4, 5, 0},
  {SOUTH, LEFT, 1, 2, 6, 7},
  {WEST, RIGHT, 9, 0, 0, 0},
  {WEST, LEFT, 3, 4, 6, 8}
};

/*
 * supply_arrivals()
 *
 * A function for supplying arrivals to the intersection
 * This should be executed by a separate thread
 */
static void* supply_arrivals()
{
  fprintf(stderr, "(Supplier):\t Started\n");
  int t = 0;
  int num_curr_arrivals[4][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};

  // for every arrival in the list
  for (int i = 0; i < sizeof(input_arrivals)/sizeof(Arrival); i++)
  {
    // get the next arrival in the list
    Arrival arrival = input_arrivals[i];
    fprintf(stderr, "(Supplier):\t Next arrival (%d): %d / %d @ t%d\n", arrival.id, arrival.side, arrival.direction, arrival.time);
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
 * Returns whether all arrivals have been handled based on the semaphores and mutexes
 */
static bool all_cars_handled()
{
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      int sem_value;
      sem_getvalue(&semaphores[i][j], &sem_value);
      if (sem_value > 0)
      {
        return false;
      }
    }
  }
  for (int i = 0; i < sizeof(mutexes)/sizeof(mutexes[0]); i++)
  {
    if (pthread_mutex_trylock(&mutexes[i]) != 0)
    {
      return false;
    }
  }
  return true;
}

/*
 * manage_light(void* arg)
 *
 * Description:
 * A function that implements the behavior of a traffic light.
 * Receives the index of the traffic light in the lights array as an argument.
 * While not all arrivals have been handled, repeatedly:
 * - Waits for an arrival using the semaphore for this traffic light.
 * - Locks the mutex(es) associated with the relevant intersection sections.
 * - Checks if all relevant intersection sections are unlocked; if yes, makes the traffic light turn green.
 * - Sleeps for CROSS_TIME seconds while the car passes.
 * - Makes the traffic light turn red and unlocks the relevant intersection section mutexes.
 */
static void* manage_light(void* arg)
{
  int light_index = (int)arg;
  Side side = lights[light_index].side;
  Direction direction = lights[light_index].direction;
  int m1 = lights[light_index].m1;
  int m2 = lights[light_index].m2;
  int m3 = lights[light_index].m3;
  int m4 = lights[light_index].m4;
  fprintf(stderr, "(Light %d / %d):\t Started\n", side, direction);

  // keep track of how many cars have passed
  int cars_passed = 0;

  // work until controller kills the thread
  while (true)
  {
    // wait for an arrival
    sem_wait(&semaphores[side][direction]);

    fprintf(stderr, "(Light %d / %d):\t Car %d arrived at light\n", side, direction, curr_arrivals[side][direction][cars_passed].id);

    // lock the section change lock
    pthread_mutex_lock(&mutex_lock);

    fprintf(stderr, "(Light %d / %d):\t Section change lock locked\n", side, direction);
    
    // for each path_mutex check whether it is locked
    bool all_unlocked = true;
    if (m1 != 0)
    {
      if (pthread_mutex_trylock(&mutexes[m1]) != 0)
      {
        all_unlocked = false;
      }
    }
    fprintf(stderr, "(Light %d / %d):\t 1/4 unlocked: %d\n", side, direction, all_unlocked);
    if (m2 != 0)
    {
      if (pthread_mutex_trylock(&mutexes[m2]) != 0)
      {
        all_unlocked = false;
      }
    }
    fprintf(stderr, "(Light %d / %d):\t 2/4 unlocked: %d\n", side, direction, all_unlocked);
    if (m3 != 0)
    {
      if (pthread_mutex_trylock(&mutexes[m3]) != 0)
      {
        all_unlocked = false;
      }
    }
    fprintf(stderr, "(Light %d / %d):\t 3/4 unlocked: %d\n", side, direction, all_unlocked);
    if (m4 != 0)
    {
      if (pthread_mutex_trylock(&mutexes[m4]) != 0)
      {
        all_unlocked = false;
      }
    }
    fprintf(stderr, "(Light %d / %d):\t 4/4 unlocked: %d\n", side, direction, all_unlocked);

    // if all path_mutexes are unlocked, make the light green
    if (all_unlocked)
    {
      // print the light change
      print_traffic_light_change(side, direction, true, get_time_passed(), curr_arrivals[side][direction][cars_passed].id);

      fprintf(stderr, "(Light %d / %d):\t Path mutexes locked\n", side, direction);

      // unlock the section change lock
      pthread_mutex_unlock(&mutex_lock);

      fprintf(stderr, "(Light %d / %d):\t Section change lock unlocked\n", side, direction);

      // sleep for CROSS_TIME seconds
      sleep(CROSS_TIME);

      fprintf(stderr, "(Light %d / %d):\t Car %d passed\n", side, direction, curr_arrivals[side][direction][cars_passed].id);

      // print the light change
      print_traffic_light_change(side, direction, false, get_time_passed(), 0);

      // lock the section change lock
      pthread_mutex_lock(&mutex_lock);

      fprintf(stderr, "(Light %d / %d):\t Section change lock locked\n", side, direction);

      // unlock the path_mutexes
      if (m1 != 0)
      {
        pthread_mutex_unlock(&mutexes[m1]);
      }
      if (m2 != 0)
      {
        pthread_mutex_unlock(&mutexes[m2]);
      }
      if (m3 != 0)
      {
        pthread_mutex_unlock(&mutexes[m3]);
      }
      if (m4 != 0)
      {
        pthread_mutex_unlock(&mutexes[m4]);
      }

      fprintf(stderr, "(Light %d / %d):\t Path mutexes unlocked\n", side, direction);

      // increment the number of cars passed
      cars_passed += 1;
    }
    else
    {
      // increment the semaphore again so that the light can try again later when the section is unlocked
      sem_post(&semaphores[side][direction]);
    }

    // unlock the section change lock
    pthread_mutex_unlock(&mutex_lock);
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

  // Initialize each mutex in the array
  for (int i = 0; i < 8; ++i) {
    pthread_mutex_init(&mutexes[i], NULL);
  }

  // create a thread per traffic light that executes manage_light
  pthread_t light_threads[8];
  fprintf(stderr, "(Controller):\t Creating traffic light threads...\n");
  for (int i = 0; i < sizeof(lights)/sizeof(lights[0]); i++)
  {
    pthread_create(&light_threads[i], NULL, manage_light, (void*)i);
  }
  fprintf(stderr, "(Controller):\t Traffic light threads created\n");

  // start the timer
  fprintf(stderr, "(Controller):\t Starting timer...\n");
  start_time();
  fprintf(stderr, "(Controller):\t Timer started\n");

  // create a thread that executes supply_arrivals
  pthread_t arrival_thread;
  fprintf(stderr, "(Controller):\t Creating arrival thread...\n");
  pthread_create(&arrival_thread, NULL, supply_arrivals, NULL);
  fprintf(stderr, "(Controller):\t Arrival thread created\n");

  // wait for all arrivals to finish
  fprintf(stderr, "(Controller):\t Waiting for threads to finish...\n");
  pthread_join(arrival_thread, NULL);
  fprintf(stderr, "(Controller):\t Arrival thread finished\n");

  // wait for all cars to be handled
  while (!all_cars_handled())
  {
    sleep(1);
  }

  fprintf(stderr, "(Controller):\t All cars handled\n");

  // kill all traffic light threads
  fprintf(stderr, "(Controller):\t Killing traffic light threads...\n");
  for (int i = 0; i < sizeof(lights)/sizeof(lights[0]); i++)
  {
    pthread_cancel(light_threads[i]);
  }
  fprintf(stderr, "(Controller):\t Traffic light threads killed\n");

  // destroy semaphores
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      sem_destroy(&semaphores[i][j]);
    }
  }
  // destroy mutexes
  for (int i = 0; i < 8; ++i) {
    pthread_mutex_destroy(&mutexes[i]);
  }
}
