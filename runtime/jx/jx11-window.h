#ifndef OSAURA_JX11_WINDOW_H
#define OSAURA_JX11_WINDOW_H

#include <stdint.h>

#define OSAURA_JX11_WINDOW_MAX 64u
#define OSAURA_JX11_WINDOW_NONE UINT32_MAX
#define OSAURA_JX11_EVENT_QUEUE_MAX 128u

typedef enum {
    OSAURA_JX11_EVENT_NONE = 0,
    OSAURA_JX11_EVENT_FOCUS,
    OSAURA_JX11_EVENT_BLUR,
    OSAURA_JX11_EVENT_POINTER_MOVE,
    OSAURA_JX11_EVENT_POINTER_DOWN,
    OSAURA_JX11_EVENT_POINTER_UP,
    OSAURA_JX11_EVENT_KEY_DOWN,
    OSAURA_JX11_EVENT_KEY_UP,
    OSAURA_JX11_EVENT_MOVE,
    OSAURA_JX11_EVENT_RESIZE,
    OSAURA_JX11_EVENT_CLOSE
} osaura_jx11_event_type;

typedef struct {
    uint32_t window_id;
    uint32_t owner_subject;
    uint32_t parent_id;
    uint32_t surface_id;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    int32_t z;
    uint8_t visible;
    uint8_t focused;
} osaura_jx11_window_info;

typedef struct {
    uint32_t type;
    uint32_t window_id;
    uint32_t owner_subject;
    int32_t x;
    int32_t y;
    uint32_t code;
    uint32_t value;
} osaura_jx11_event;

int osaura_jx11_window_init(void);
void osaura_jx11_window_shutdown(void);
int osaura_jx11_window_create(uint32_t owner_subject,
                              uint32_t parent_id,
                              int32_t x,
                              int32_t y,
                              uint32_t width,
                              uint32_t height,
                              uint32_t *window_id);
int osaura_jx11_window_destroy_as(uint32_t owner_subject, uint32_t window_id);
int osaura_jx11_window_get_info(uint32_t window_id, osaura_jx11_window_info *out);
int osaura_jx11_window_move_as(uint32_t owner_subject, uint32_t window_id, int32_t x, int32_t y);
int osaura_jx11_window_resize_as(uint32_t owner_subject, uint32_t window_id, uint32_t width, uint32_t height);
int osaura_jx11_window_set_visible_as(uint32_t owner_subject, uint32_t window_id, int visible);
int osaura_jx11_window_raise_as(uint32_t owner_subject, uint32_t window_id);
int osaura_jx11_window_lower_as(uint32_t owner_subject, uint32_t window_id);
int osaura_jx11_window_focus_as(uint32_t owner_subject, uint32_t window_id);
uint32_t osaura_jx11_window_focused(void);
uint32_t osaura_jx11_window_hit_test(int32_t x, int32_t y);
int osaura_jx11_window_pointer(int32_t x, int32_t y, uint32_t buttons, uint32_t changed_button, int pressed);
int osaura_jx11_window_key(uint32_t code, int pressed);
int osaura_jx11_window_event_pop(uint32_t owner_subject, osaura_jx11_event *event);
int osaura_jx11_window_compose(void);
uint32_t osaura_jx11_window_count(void);

#endif
