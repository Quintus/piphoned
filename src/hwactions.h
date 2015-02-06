#ifndef PIPHONED_HWACTIONS_H
#define PIPHONED_HWACTIONS_H
#include <stdbool.h>

void piphoned_hwactions_init();                 /*< Initialize interrupt callbacks. */
void piphoned_hwactions_free();                 /*< Cleanup all the callbacks */
bool piphoned_hwactions_is_phone_hung_up();     /*< Is the phone on the base? */
void piphoned_hwactions_get_sip_uri(char* target); /*< Get the URI dialed. */

#endif
