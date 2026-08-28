#include "usb-hot.h"
#include "hot-shadow.h"

#include <stdint.h>

static int hot_usb_init(void *context, void *request) {
    (void)context;
    (void)request;
    return osaura_usb_init();
}

static int hot_usb_poll(void *context, void *request) {
    (void)context;
    (void)request;
    osaura_usb_poll();
    return 1;
}

static int hot_usb_event_pop(void *context, void *request) {
    (void)context;
    return osaura_usb_keyboard_event_pop((osaura_key_event *)request);
}

static int hot_usb_char_pop(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    *(char *)request = osaura_usb_keyboard_pop();
    return *(char *)request ? 1 : 0;
}

static int hot_usb_xhci_present(void *context, void *request) {
    (void)context;
    (void)request;
    return osaura_usb_xhci_present();
}

static int hot_usb_hid_ready(void *context, void *request) {
    (void)context;
    (void)request;
    return osaura_usb_keyboard_ready();
}

static int hot_usb_status(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    osaura_usb_hot_status *status = (osaura_usb_hot_status *)request;
    status->xhci_present = (uint8_t)(osaura_usb_xhci_present() != 0);
    status->keyboard_ready = (uint8_t)(osaura_usb_keyboard_ready() != 0);
    return 1;
}

static int hot_usb_reinit(void *context, void *request) {
    (void)context;
    (void)request;
    return osaura_usb_init();
}

int osaura_usb_hot_bind(void) {
    int rc = 0;
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_USB, OSAURA_USB_HOT_INIT, hot_usb_init, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_USB, OSAURA_USB_HOT_POLL, hot_usb_poll, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_USB, OSAURA_USB_HOT_EVENT_POP, hot_usb_event_pop, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_USB, OSAURA_USB_HOT_CHAR_POP, hot_usb_char_pop, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_USB, OSAURA_USB_HOT_XHCI_PRESENT, hot_usb_xhci_present, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_USB, OSAURA_USB_HOT_HID_READY, hot_usb_hid_ready, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_USB, OSAURA_USB_HOT_STATUS, hot_usb_status, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_USB, OSAURA_USB_HOT_REINIT, hot_usb_reinit, 0);
    return rc;
}

int osaura_usb_hot_init(void) {
    return osaura_hot_dispatch(OSAURA_HOT_BANK_USB, OSAURA_USB_HOT_INIT, 0);
}

void osaura_usb_hot_poll(void) {
    (void)osaura_hot_dispatch(OSAURA_HOT_BANK_USB, OSAURA_USB_HOT_POLL, 0);
}

int osaura_usb_hot_event_pop(osaura_key_event *event) {
    return osaura_hot_dispatch(OSAURA_HOT_BANK_USB, OSAURA_USB_HOT_EVENT_POP, event);
}

char osaura_usb_hot_char_pop(void) {
    char out = 0;
    (void)osaura_hot_dispatch(OSAURA_HOT_BANK_USB, OSAURA_USB_HOT_CHAR_POP, &out);
    return out;
}

int osaura_usb_hot_xhci_present(void) {
    return osaura_hot_dispatch(OSAURA_HOT_BANK_USB, OSAURA_USB_HOT_XHCI_PRESENT, 0);
}

int osaura_usb_hot_keyboard_ready(void) {
    return osaura_hot_dispatch(OSAURA_HOT_BANK_USB, OSAURA_USB_HOT_HID_READY, 0);
}

int osaura_usb_hot_status_read(osaura_usb_hot_status *status) {
    return osaura_hot_dispatch(OSAURA_HOT_BANK_USB, OSAURA_USB_HOT_STATUS, status);
}

int osaura_usb_hot_reinit(void) {
    return osaura_hot_dispatch(OSAURA_HOT_BANK_USB, OSAURA_USB_HOT_REINIT, 0);
}
