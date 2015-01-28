/* The code in this file is heavily inspired by the wiringPi sourcecode,
 * although it it not a copy of it. Original wiringPi copyright statement:
 *
 *  wiringPi:
 *	Arduino compatable (ish) Wiring library for the Raspberry Pi
 *	Copyright (c) 2012 Gordon Henderson
 *	Additional code for pwmSetClock by Chris Hall <chris@kchall.plus.com>
 *
 *    https://projects.drogon.net/raspberry-pi/wiringpi/
 *
 *    wiringPi is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as
 *    published by the Free Software Foundation, either version 3 of the
 *    License, or (at your option) any later version.
 *
 *    wiringPi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with wiringPi.
 *    If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <poll.h>
#include <linux/limits.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <wiringPi.h>
#include "interrupt_handler.h"

/**
 * Private struct for the data that has to be passed into the
 * pthread thread function. Note it encapsulates the user data
 * passed to piphoned_handle_pin_interrupt()!
 */
struct Piphoned_InterruptHandler_Data
{
  int pin;                        /*< wiringPi pin number */
  int hardware_pin;               /*< BCM GPIO hardware pin number corresponding to `pin` */
  void (*p_callback)(int, void*); /*< Sub-callback for the user-defined action to take */
  void* p_userdata;               /*< Custom userdata pointer passed through to the sub-callback */
};

static void* device_interrupt_handler(void* arg);

static int s_sysfs_fds[64]= { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
static pthread_t s_threads[64];
static struct Piphoned_InterruptHandler_Data s_interrupt_handler_datas[64];

/**
 * Waits for an interrupt and executes a callback.
 *
 * This function waits for an interrupt on the given wiringPi pin of the
 * given nature, see docs of wiringPiISR(). This function is very similar
 * to that one, except it gives you more freedom in defining the callback,
 * because it passes both the pin that the callback function is run for and
 * a custom data pointer to the callback function.
 *
 * The waiting for the interrupt is done in a separate thread, and thus, when
 * the interrupt happens, the callback function is run in that separate thread.
 * If you access a shared resource, be sure to protect it with a mutex.
 *
 * \param pin WiringPi pin number to wait on.
 * \param edge_type INT_EDGE_FALLING, INT_EDGE_RISING, INT_EDGE_BOTH, or INT_EDGE_SETUP.
 *                  See the documentation of wiringPiISR().
 * \param[in] p_callback Function pointer to a callback function that is executed
 *                       each time the interrupt fires. It gets passed the pin
 *                       on which the interrupt was received and custom userdata
 *                       (see below).
 * \param[in] p_userdata Custom userdata pointer. This is passed to the callback
 *                       function as-is without further modificatons.
 *
 * \returns true if the callback was successfully registered and we are waiting
 * for interrupts now, false otherwise.
 *
 * \remark The callback function is executed for every single interrupt received.
 * If multiple interrupts are received while the callback is running, it will
 * be executed immediately again.
 */
bool piphoned_handle_pin_interrupt(int pin, int edge_type, void (*p_callback)(int, void*), void* p_userdata)
{
  int hardware_pin = -1;
  int count = 0;
  int i = 0;
  const char* modestr = NULL;
  char sysfs_path[PATH_MAX];

  if (pin < 0 || pin > 63)
    return false;

  hardware_pin = wpiPinToGpio(pin);

  switch (edge_type) {
  case INT_EDGE_FALLING:
    modestr = "falling";
    break;
  case INT_EDGE_RISING:
    modestr = "rising";
    break;
  case INT_EDGE_BOTH:
    modestr = "both";
    break;
  case INT_EDGE_SETUP:
    /* Nothing */
    break;
  default: /* Invalid */
    return false;
  }

  if (s_sysfs_fds[hardware_pin] != -1) {
    syslog(LOG_ERR, "Can only register one callback handler per pin (pin num was %d).", pin);
    return false;
  }

  syslog(LOG_DEBUG, "Registering lowlevel interrupt handler on pin %d (BCM GPIO pin %d)", pin, hardware_pin);

  /* Set up a /sys device that blocks until data from the outside is available.
   * This is done using the wiringPi gpio program.
   * TODO: Get rid of the external call. */
  if (edge_type != INT_EDGE_SETUP) {
    char command[256];
    int status = 0;

    memset(command, '\0', 256);
    sprintf(command, "gpio edge %d %s", hardware_pin, modestr);

    syslog(LOG_DEBUG, "Setting up /sys node for kernel interrupt (%s)", command);

    if ((status = system(command)) != 0) { /* Single = intended */
      syslog(LOG_ERR, "Executing '%s' failed with status '%d'.", command, status);
      return false;
    }
  }

  memset(sysfs_path, '\0', PATH_MAX);
  sprintf(sysfs_path, "/sys/class/gpio/gpio%d/value", hardware_pin);

  s_sysfs_fds[hardware_pin] = open(sysfs_path, O_RDWR);
  if (s_sysfs_fds[hardware_pin] < 0) {
    syslog(LOG_ERR, "Failed to open sysfs path '%s': %m", sysfs_path);
    return false;
  }

  /* Ensure we don’t get triggered right at the beginning due to a pending interrupt
   * by simply discarding all data available in the device. */
  ioctl(s_sysfs_fds[hardware_pin], FIONREAD, &count);
  for (i=0; i < count; i++) {
    char data;
    read(s_sysfs_fds[hardware_pin], &data, 1);
  }

  s_interrupt_handler_datas[hardware_pin].pin          = pin;
  s_interrupt_handler_datas[hardware_pin].hardware_pin = hardware_pin;
  s_interrupt_handler_datas[hardware_pin].p_callback   = p_callback;
  s_interrupt_handler_datas[hardware_pin].p_userdata   = p_userdata;

  syslog(LOG_DEBUG, "Spawning new thread for monitoring the hardware device '%s'", sysfs_path);
  if (pthread_create(&s_threads[hardware_pin], NULL, device_interrupt_handler, &s_interrupt_handler_datas[hardware_pin]) != 0) {
    syslog(LOG_ERR, "Failed to start interrupt handler thread: %m");
    return false;
  }

  return true;
}

/**
 * This function is run as the thread that watches a GPIO device node.
 * If the device triggers, a callback function is executed. Processing
 * starts again after the callback completes.
 *
 * Note that in case of multiple consecutive interrupts that happen while
 * the callback is still running, the callback will be executed immediately
 * again and again, until all interrupts have been handled.
 */
void* device_interrupt_handler(void* arg)
{
  struct Piphoned_InterruptHandler_Data* p_handler_data = (struct Piphoned_InterruptHandler_Data*) arg;
  struct pollfd polldata;
  int fd = 0;
  char data;

  syslog(LOG_DEBUG, "Spawned thread successfully");
  fd = s_sysfs_fds[p_handler_data->hardware_pin];

  polldata.fd = fd;
  polldata.events = POLLPRI;

  syslog(LOG_DEBUG, "Entering lowlevel interrupt loop for file descriptor %d", fd);
  while(true) {
    if (poll(&polldata, 1, -1) < 0) {
      syslog(LOG_ERR, "Failed to poll() data from pin %d: %m", p_handler_data->pin);
      continue;
    }

    /* TODO: Add timeout to poll() so we can gracefully terminate the thread.
     * We could malloc() the stuff that is `static' currently and free() it then
     * on thread termination. */

    /* We only wait for something to appear. What it is is not important. Read
     * the data, discard it. wiringPi sourcecode comments say it will only ever be
     * one char, so consume that. */
    read(fd, &data, 1); /* Ignore failure */
    lseek(fd, 0, SEEK_SET);

    /* Call the callback function */
    p_handler_data->p_callback(p_handler_data->pin, p_handler_data->p_userdata);
  }

  return NULL;
}
