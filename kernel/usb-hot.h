#ifndef OSAURA_USB_HOT_H
#define OSAURA_USB_HOT_H

#include <stdint.h>
#include "usb.h"

/* USB owns bank 6: 0xB0..0xB7. These are the eight operations that sit on
 * top of the raw xHCI/HID mechanisms. The raw driver remains the authority;
 * this layer only gives the kernel/JX one-byte prepared entry points. */
enum {
    OSAURA_USB_HOT_INIT = 0u,          /* 0xB0 controller discover/reset/start */
    OSAURA_USB_HOT_POLL = 1u,          /* 0xB1 service xHCI/HID completion */
    OSAURA_USB_HOT_EVENT_POP = 2u,     /* 0xB2 structured key event */
    OSAURA_USB_HOT_CHAR_POP = 3u,      /* 0xB3 printable compatibility byte */
    OSAURA_USB_HOT_XHCI_PRESENT = 4u,  /* 0xB4 controller presence */
    OSAURA_USB_HOT_HID_READY = 5u,     /* 0xB5 boot keyboard ready */
    OSAURA_USB_HOT_STATUS = 6u,        /* 0xB6 packed public USB state */
    OSAURA_USB_HOT_REINIT = 7u         /* 0xB7 explicit recovery re-init */
};

typedef struct {
    uint8_t xhci_present;
    uint8_t keyboard_ready;
} osaura_usb_hot_status;

/* Called by the global hot-map bootstrap. Safe to call more than once. */
int osaura_usb_hot_bind(void);

/* Direct one-byte dispatch helpers for native callers that want the hot ABI. */
int osaura_usb_hot_init(void);
void osaura_usb_hot_poll(void);
int osaura_usb_hot_event_pop(osaura_key_event *event);
char osaura_usb_hot_char_pop(void);
int osaura_usb_hot_xhci_present(void);
int osaura_usb_hot_keyboard_ready(void);
int osaura_usb_hot_status_read(osaura_usb_hot_status *status);
int osaura_usb_hot_reinit(void);

#endif
