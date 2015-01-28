#include <time.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/time.h>
#include <wiringPi.h>
#include "hwactions.h"
#include "configfile.h"

static pthread_mutex_t s_hangup_mutex;  /*< Mutex for the hangup callback function */
static bool s_hangup_triggered = false; /*< true if hangup trigger has been triggered */
static struct timeval s_hangup_timestamp;
static unsigned long s_hangup_microseconds_now = 0;
static unsigned long s_hangup_microseconds_last = 0;

static void hangup_callback();

/**
 * Sets up the callbacks for the interrupts on the Raspberry Pi’s pins.
 */
void piphoned_hwactions_init()
{
  /* TODO: Call piphoned_hwactions_triggermonitor_new() and _setup()
   * for the target pins */
    /* Ensure the pins are in a predictable start state */
  /*pinMode(g_piphoned_config_info.hangup_pin, OUTPUT);
  digitalWrite(g_piphoned_config_info.hangup_pin, LOW);
  pinMode(g_piphoned_config_info.hangup_pin, INPUT);

  wiringPiISR(g_piphoned_config_info.hangup_pin, INT_EDGE_BOTH, hangup_callback); */
}

/**
 * Checks if the hangup trigger has been triggered. The variable used internally
 * is a resource that is shared with the interrupt function thread, so this
 * function first acquires the necessary mutex lock, then queries the variable,
 * **resets it to false**, releases the lock, and then returns what was found.
 * That is, after you got true from this function, the state is guaranteed
 * to be "not triggered" again. You never get true by two consecutive calls
 * to this function unless someone pulls the hangup trigger on the phone
 * in that very microsecond between the two calls.
 *
 * Because the function abstracts the threads from you, you don’t have to
 * worry about the underlying interrupt thread.
 *
 * This function makes no difference between releasing the trigger and
 * pressing it. When either has happened, it will return true.
 */
bool piphoned_hwactions_has_hangup_triggered()
{
  bool result = false;

  /*
  pthread_mutex_lock(&s_hangup_mutex);
  result = s_hangup_triggered;
  s_hangup_triggered = false; /* Reset so we will be notified of new hangup triggering /
  pthread_mutex_unlock(&s_hangup_mutex);
  */

  return result;
}

/**
 * Checks if an URI has been dialed, and if so, places it in the passed argument.
 * Returns true if an URI was dialed (and placed in the pointer), false
 * otherwise.
 */
bool piphoned_hwactions_has_dialed_uri(char* uri)
{

}

/**
 * Called each time the hangup trigger connects or disconnects. Runs
 * in a separate thread.
 */
void hangup_callback()
{
  pthread_mutex_lock(&s_hangup_mutex);

  /* The trigger connects and disconnects in a very short period of time
   * multiple times. If you hangup, it triggers about 10 times in a single
   * second, whilst we of course only want to count this as a single hangup
   * signal. Thus, I use a grace time below that has to pass before we
   * run this callback’s main code at all again. The exact time that is
   * waited below has been found by trial&error. */
  gettimeofday(&s_hangup_timestamp, NULL);
  s_hangup_microseconds_now = s_hangup_timestamp.tv_sec * 1000000 + s_hangup_timestamp.tv_usec;
  if (s_hangup_microseconds_now - s_hangup_microseconds_last <= 100000) /* Grace time */
    goto release_hangup_lock;

  s_hangup_triggered = true;

  /* Update timestamp for our grace time check above */
  s_hangup_microseconds_last = s_hangup_microseconds_now;

 release_hangup_lock:
  pthread_mutex_unlock(&s_hangup_mutex);
}
