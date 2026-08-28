#ifndef OSAURA_JX11_WINDOW_BAG_H
#define OSAURA_JX11_WINDOW_BAG_H

#include <stdint.h>
#include <stddef.h>

#define OSAURA_JX11_WINDOW_BAG_NONE 0ull

typedef struct {
    uint32_t window_id;
    uint32_t owner_subject;
    uint64_t bag_id;
    uint64_t bag_generation;
    const void *borrowed_data;
    uint32_t borrowed_bytes;
    uint8_t bound;
} osaura_jx11_window_bag_view;

/* Window/view state borrows canonical Bag data; it never owns or copies it. */
int osaura_jx11_window_bag_init(void);
void osaura_jx11_window_bag_shutdown(void);
int osaura_jx11_window_bag_bind_as(uint32_t owner_subject,
                                   uint32_t window_id,
                                   uint64_t bag_id,
                                   uint64_t bag_generation,
                                   const void *borrowed_data,
                                   uint32_t borrowed_bytes);
int osaura_jx11_window_bag_rebind_as(uint32_t owner_subject,
                                     uint32_t window_id,
                                     uint64_t bag_generation,
                                     const void *borrowed_data,
                                     uint32_t borrowed_bytes);
int osaura_jx11_window_bag_unbind_as(uint32_t owner_subject, uint32_t window_id);
int osaura_jx11_window_bag_get(uint32_t window_id, osaura_jx11_window_bag_view *out);

/* Publish a semantic Bag generation through the processor bus without copying it. */
int osaura_jx11_window_bag_publish_as(uint32_t owner_subject,
                                      uint32_t window_id,
                                      uint32_t change_kind,
                                      uint32_t source_pid,
                                      uint32_t flags);

#endif
