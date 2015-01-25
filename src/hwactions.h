#ifndef PIPHONED_HWACTIONS_H
#define PIPHONED_HWACTIONS_H
#include <stdbool.h>

void piphoned_hwactions_init();                 /*< Initialize wiringPi callbacks. */
bool piphoned_hwactions_has_hangup_triggered(); /*< Has the hangup trigger been activated? */

#endif
