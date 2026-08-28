#ifndef OSAURA_USB_KEYBOARD_H
#define OSAURA_USB_KEYBOARD_H

#include <stdint.h>

int osaura_usb_keyboard_init(void);
int osaura_usb_keyboard_ready(void);
uint32_t osaura_usb_keyboard_stage(void);
void osaura_usb_keyboard_poll(void);

#endif
