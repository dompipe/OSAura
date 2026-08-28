#ifndef OSAURA_USB_H
#define OSAURA_USB_H

#include <stdint.h>

/*
 * Native pre-JX11 USB input layer.
 *
 * Phase 1 owns/discovers xHCI and exposes one keyboard-event source.  The
 * terminal consumes USB and PS/2 through the same character path; later the
 * event API can grow modifier/key-up information for Alt+Tab and JX11.
 */
int osaura_usb_init(void);
void osaura_usb_poll(void);
int osaura_usb_xhci_present(void);
int osaura_usb_keyboard_ready(void);
char osaura_usb_keyboard_pop(void);

#endif
