#ifndef PIPHONED_INTERRUPT_HANDLER_H
#define PIPHONED_INTERRUPT_HANDLER_H

bool piphoned_handle_pin_interrupt(int pin, int edge_type, void (*p_callback)(int, void*), void* p_userdata);
void piphoned_terminate_pin_interrupt_handler(int pin);

#endif
