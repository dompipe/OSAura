#ifndef OSAURA_USB_HOT_H
#define OSAURA_USB_HOT_H

#include <stdint.h>
#include "usb.h"

enum {
    OSAURA_USB_HOT_INIT = 0u,
    OSAURA_USB_HOT_POLL = 1u,
    OSAURA_USB_HOT_EVENT_POP = 2u,
    OSAURA_USB_HOT_CHAR_POP = 3u,
    OSAURA_USB_HOT_XHCI_PRESENT = 4u,
    OSAURA_USB_HOT_HID_READY = 5u,
    OSAURA_USB_HOT_STATUS = 6u,
    OSAURA_USB_HOT_REINIT = 7u
};

typedef struct {
    uint8_t xhci_present;
    uint8_t keyboard_ready;
} osaura_usb_hot_status;

typedef struct {
    uint32_t subject;
} osaura_usb_hot_control_request;

int osaura_usb_hot_bind(void);
int osaura_usb_hot_init_as(uint32_t subject);
int osaura_usb_hot_reinit_as(uint32_t subject);
int osaura_usb_hot_init(void);
void osaura_usb_hot_poll(void);
int osaura_usb_hot_event_pop(osaura_key_event *event);
char osaura_usb_hot_char_pop(void);
int osaura_usb_hot_xhci_present(void);
int osaura_usb_hot_keyboard_ready(void);
int osaura_usb_hot_status_read(osaura_usb_hot_status *status);
int osaura_usb_hot_reinit(void);

#endif
