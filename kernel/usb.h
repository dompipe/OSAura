#ifndef OSAURA_USB_H
#define OSAURA_USB_H

#include <stdint.h>

#define OSAURA_KEY_MOD_CTRL  0x01u
#define OSAURA_KEY_MOD_SHIFT 0x02u
#define OSAURA_KEY_MOD_ALT   0x04u
#define OSAURA_KEY_MOD_GUI   0x08u

#define OSAURA_KEY_TAB       0x2bu
#define OSAURA_KEY_ENTER     0x28u
#define OSAURA_KEY_BACKSPACE 0x2au

typedef struct {
    uint8_t usage;
    uint8_t modifiers;
    uint8_t pressed;
    char character;
} osaura_key_event;

/* Native pre-JX11 USB input layer. xHCI owns USB HID boot keyboards and
 * publishes modifier-preserving key events. PS/2 can feed the same terminal
 * policy above this layer; JX11 is not required for keyboard switching. */
int osaura_usb_init(void);
void osaura_usb_poll(void);
int osaura_usb_xhci_present(void);
int osaura_usb_keyboard_ready(void);
int osaura_usb_keyboard_event_pop(osaura_key_event *event);
char osaura_usb_keyboard_pop(void);

#endif
