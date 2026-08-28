#include "jx11-surface.h"
#include "../../kernel/display.h"

#include <string.h>
#include <stdint.h>

typedef struct {
    uint32_t owner_subject;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    int32_t z;
    uint8_t opacity;
    uint8_t visible;
    uint8_t used;
    uint32_t *pixels;
} jx11_surface_slot;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t right;
    int32_t bottom;
    uint8_t valid;
} jx11_damage_rect;

static jx11_surface_slot g_surface[OSAURA_JX11_SURFACE_MAX];
static osaura_jx11_alloc_fn g_alloc;
static osaura_jx11_free_fn g_free;
static void *g_alloc_context;
static uint32_t g_background_xrgb;
static jx11_damage_rect g_damage;
static uint8_t g_ready;

static void damage_reset(void) {
    memset(&g_damage, 0, sizeof g_damage);
}

static void damage_add(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    if (!width || !height) return;
    int64_t right64 = (int64_t)x + (int64_t)width;
    int64_t bottom64 = (int64_t)y + (int64_t)height;
    if (right64 <= 0 || bottom64 <= 0) return;
    if (right64 > INT32_MAX) right64 = INT32_MAX;
    if (bottom64 > INT32_MAX) bottom64 = INT32_MAX;
    int32_t right = (int32_t)right64;
    int32_t bottom = (int32_t)bottom64;
    if (!g_damage.valid) {
        g_damage.x = x;
        g_damage.y = y;
        g_damage.right = right;
        g_damage.bottom = bottom;
        g_damage.valid = 1u;
        return;
    }
    if (x < g_damage.x) g_damage.x = x;
    if (y < g_damage.y) g_damage.y = y;
    if (right > g_damage.right) g_damage.right = right;
    if (bottom > g_damage.bottom) g_damage.bottom = bottom;
}

static int valid_owned(uint32_t owner, uint32_t id, jx11_surface_slot **slot_out) {
    if (!g_ready || id >= OSAURA_JX11_SURFACE_MAX) return -1;
    jx11_surface_slot *slot = &g_surface[id];
    if (!slot->used) return -2;
    if (slot->owner_subject != owner) return -3;
    if (slot_out) *slot_out = slot;
    return 0;
}

static uint8_t channel_blend(uint8_t dst, uint8_t src, uint32_t alpha) {
    return (uint8_t)(((uint32_t)src * alpha + (uint32_t)dst * (255u - alpha) + 127u) / 255u);
}

static uint32_t blend_argb_over_xrgb(uint32_t dst_xrgb, uint32_t src_argb, uint8_t opacity) {
    uint32_t src_a = (src_argb >> 24) & 0xffu;
    uint32_t alpha = (src_a * (uint32_t)opacity + 127u) / 255u;
    if (!alpha) return dst_xrgb;
    if (alpha >= 255u) return src_argb & 0x00ffffffu;
    uint8_t dr = (uint8_t)((dst_xrgb >> 16) & 0xffu);
    uint8_t dg = (uint8_t)((dst_xrgb >> 8) & 0xffu);
    uint8_t db = (uint8_t)(dst_xrgb & 0xffu);
    uint8_t sr = (uint8_t)((src_argb >> 16) & 0xffu);
    uint8_t sg = (uint8_t)((src_argb >> 8) & 0xffu);
    uint8_t sb = (uint8_t)(src_argb & 0xffu);
    return ((uint32_t)channel_blend(dr, sr, alpha) << 16) |
           ((uint32_t)channel_blend(dg, sg, alpha) << 8) |
           (uint32_t)channel_blend(db, sb, alpha);
}

static int slot_above(const jx11_surface_slot *a, uint32_t aid,
                      const jx11_surface_slot *b, uint32_t bid) {
    if (a->z != b->z) return a->z > b->z;
    return aid > bid;
}

int osaura_jx11_surface_init(osaura_jx11_alloc_fn alloc_fn,
                             osaura_jx11_free_fn free_fn,
                             void *context) {
    if (!alloc_fn || !free_fn) return -1;
    osaura_jx11_surface_shutdown();
    memset(g_surface, 0, sizeof g_surface);
    g_alloc = alloc_fn;
    g_free = free_fn;
    g_alloc_context = context;
    g_background_xrgb = 0u;
    damage_reset();
    g_ready = 1u;
    return 0;
}

void osaura_jx11_surface_shutdown(void) {
    if (g_free) {
        for (uint32_t i = 0u; i < OSAURA_JX11_SURFACE_MAX; ++i) {
            if (g_surface[i].used && g_surface[i].pixels) {
                size_t bytes = (size_t)g_surface[i].width * (size_t)g_surface[i].height * sizeof(uint32_t);
                g_free(g_surface[i].pixels, bytes, g_alloc_context);
            }
        }
    }
    memset(g_surface, 0, sizeof g_surface);
    g_alloc = 0;
    g_free = 0;
    g_alloc_context = 0;
    g_ready = 0u;
    damage_reset();
}

int osaura_jx11_surface_set_background(uint32_t color_xrgb) {
    if (!g_ready) return -1;
    g_background_xrgb = color_xrgb & 0x00ffffffu;
    const osaura_display_surface *primary = osaura_display_primary();
    if (primary) damage_add(0, 0, primary->width, primary->height);
    return 0;
}

int osaura_jx11_surface_create(uint32_t owner_subject,
                               uint32_t width,
                               uint32_t height,
                               uint32_t *surface_id) {
    if (!g_ready || !surface_id || !width || !height) return -1;
    if (width > 8192u || height > 8192u) return -2;
    uint64_t pixels64 = (uint64_t)width * (uint64_t)height;
    if (pixels64 > (uint64_t)(SIZE_MAX / sizeof(uint32_t))) return -3;
    size_t bytes = (size_t)pixels64 * sizeof(uint32_t);
    uint32_t id = OSAURA_JX11_SURFACE_NONE;
    for (uint32_t i = 0u; i < OSAURA_JX11_SURFACE_MAX; ++i) {
        if (!g_surface[i].used) { id = i; break; }
    }
    if (id == OSAURA_JX11_SURFACE_NONE) return -4;
    uint32_t *pixels = (uint32_t *)g_alloc(bytes, g_alloc_context);
    if (!pixels) return -5;
    memset(pixels, 0, bytes);
    jx11_surface_slot *slot = &g_surface[id];
    memset(slot, 0, sizeof *slot);
    slot->owner_subject = owner_subject;
    slot->width = width;
    slot->height = height;
    slot->opacity = 255u;
    slot->visible = 1u;
    slot->used = 1u;
    slot->pixels = pixels;
    damage_add(slot->x, slot->y, width, height);
    *surface_id = id;
    return 0;
}

int osaura_jx11_surface_destroy(uint32_t owner_subject, uint32_t surface_id) {
    jx11_surface_slot *slot;
    int rc = valid_owned(owner_subject, surface_id, &slot);
    if (rc != 0) return rc;
    damage_add(slot->x, slot->y, slot->width, slot->height);
    size_t bytes = (size_t)slot->width * (size_t)slot->height * sizeof(uint32_t);
    g_free(slot->pixels, bytes, g_alloc_context);
    memset(slot, 0, sizeof *slot);
    return 0;
}

int osaura_jx11_surface_get_info(uint32_t surface_id, osaura_jx11_surface_info *out) {
    if (!g_ready || !out || surface_id >= OSAURA_JX11_SURFACE_MAX) return -1;
    jx11_surface_slot *slot = &g_surface[surface_id];
    if (!slot->used) return -2;
    out->surface_id = surface_id;
    out->owner_subject = slot->owner_subject;
    out->x = slot->x;
    out->y = slot->y;
    out->width = slot->width;
    out->height = slot->height;
    out->z = slot->z;
    out->opacity = slot->opacity;
    out->visible = slot->visible;
    return 0;
}

int osaura_jx11_surface_fill_as(uint32_t owner_subject,
                                uint32_t surface_id,
                                const osaura_jx11_surface_fill *request) {
    jx11_surface_slot *slot;
    int rc = valid_owned(owner_subject, surface_id, &slot);
    if (rc != 0 || !request) return rc ? rc : -4;
    if (request->x >= slot->width || request->y >= slot->height ||
        !request->width || !request->height) return 0;
    uint32_t x_end = request->width > slot->width - request->x ? slot->width : request->x + request->width;
    uint32_t y_end = request->height > slot->height - request->y ? slot->height : request->y + request->height;
    for (uint32_t y = request->y; y < y_end; ++y) {
        uint32_t *row = slot->pixels + (uint64_t)y * slot->width;
        for (uint32_t x = request->x; x < x_end; ++x) row[x] = request->color_argb;
    }
    if (slot->visible) damage_add(slot->x + (int32_t)request->x,
                                  slot->y + (int32_t)request->y,
                                  x_end - request->x, y_end - request->y);
    return 0;
}

int osaura_jx11_surface_blit_as(uint32_t owner_subject,
                                uint32_t surface_id,
                                const osaura_jx11_surface_blit *request) {
    jx11_surface_slot *slot;
    int rc = valid_owned(owner_subject, surface_id, &slot);
    if (rc != 0 || !request || !request->pixels_argb || !request->stride_pixels) return rc ? rc : -4;
    if (request->stride_pixels < request->width) return -5;
    if (request->x >= slot->width || request->y >= slot->height ||
        !request->width || !request->height) return 0;
    uint32_t copy_w = request->width > slot->width - request->x ? slot->width - request->x : request->width;
    uint32_t copy_h = request->height > slot->height - request->y ? slot->height - request->y : request->height;
    for (uint32_t y = 0u; y < copy_h; ++y) {
        uint32_t *dst = slot->pixels + (uint64_t)(request->y + y) * slot->width + request->x;
        const uint32_t *src = request->pixels_argb + (uint64_t)y * request->stride_pixels;
        memcpy(dst, src, (size_t)copy_w * sizeof(uint32_t));
    }
    if (slot->visible) damage_add(slot->x + (int32_t)request->x,
                                  slot->y + (int32_t)request->y,
                                  copy_w, copy_h);
    return 0;
}

int osaura_jx11_surface_move_as(uint32_t owner_subject,
                                uint32_t surface_id,
                                int32_t x,
                                int32_t y) {
    jx11_surface_slot *slot;
    int rc = valid_owned(owner_subject, surface_id, &slot);
    if (rc != 0) return rc;
    if (slot->visible) damage_add(slot->x, slot->y, slot->width, slot->height);
    slot->x = x;
    slot->y = y;
    if (slot->visible) damage_add(slot->x, slot->y, slot->width, slot->height);
    return 0;
}

int osaura_jx11_surface_set_z_as(uint32_t owner_subject,
                                 uint32_t surface_id,
                                 int32_t z) {
    jx11_surface_slot *slot;
    int rc = valid_owned(owner_subject, surface_id, &slot);
    if (rc != 0) return rc;
    if (slot->visible) damage_add(slot->x, slot->y, slot->width, slot->height);
    slot->z = z;
    return 0;
}

int osaura_jx11_surface_set_opacity_as(uint32_t owner_subject,
                                       uint32_t surface_id,
                                       uint8_t opacity) {
    jx11_surface_slot *slot;
    int rc = valid_owned(owner_subject, surface_id, &slot);
    if (rc != 0) return rc;
    slot->opacity = opacity;
    if (slot->visible) damage_add(slot->x, slot->y, slot->width, slot->height);
    return 0;
}

int osaura_jx11_surface_set_visible_as(uint32_t owner_subject,
                                       uint32_t surface_id,
                                       int visible) {
    jx11_surface_slot *slot;
    int rc = valid_owned(owner_subject, surface_id, &slot);
    if (rc != 0) return rc;
    if (slot->visible) damage_add(slot->x, slot->y, slot->width, slot->height);
    slot->visible = visible ? 1u : 0u;
    if (slot->visible) damage_add(slot->x, slot->y, slot->width, slot->height);
    return 0;
}

int osaura_jx11_surface_compose(void) {
    if (!g_ready || !osaura_display_ready()) return -1;
    if (!g_damage.valid) return 0;
    const osaura_display_surface *primary = osaura_display_primary();
    if (!primary) return -2;

    int32_t x0 = g_damage.x < 0 ? 0 : g_damage.x;
    int32_t y0 = g_damage.y < 0 ? 0 : g_damage.y;
    int32_t x1 = g_damage.right > (int32_t)primary->width ? (int32_t)primary->width : g_damage.right;
    int32_t y1 = g_damage.bottom > (int32_t)primary->height ? (int32_t)primary->height : g_damage.bottom;
    if (x0 >= x1 || y0 >= y1) { damage_reset(); return 0; }

    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)primary->framebuffer_base;
    for (int32_t y = y0; y < y1; ++y) {
        volatile uint32_t *row = fb + (uint64_t)(uint32_t)y * primary->stride_pixels;
        for (int32_t x = x0; x < x1; ++x) {
            uint32_t color = g_background_xrgb;
            uint32_t order[OSAURA_JX11_SURFACE_MAX];
            uint32_t count = 0u;
            for (uint32_t i = 0u; i < OSAURA_JX11_SURFACE_MAX; ++i) {
                jx11_surface_slot *s = &g_surface[i];
                if (!s->used || !s->visible || !s->opacity) continue;
                int64_t sx1 = (int64_t)s->x + s->width;
                int64_t sy1 = (int64_t)s->y + s->height;
                if (x < s->x || y < s->y || (int64_t)x >= sx1 || (int64_t)y >= sy1) continue;
                uint32_t pos = count;
                while (pos > 0u && slot_above(&g_surface[order[pos - 1u]], order[pos - 1u], s, i)) {
                    order[pos] = order[pos - 1u];
                    --pos;
                }
                order[pos] = i;
                ++count;
            }
            for (uint32_t oi = 0u; oi < count; ++oi) {
                jx11_surface_slot *s = &g_surface[order[oi]];
                uint32_t sx = (uint32_t)(x - s->x);
                uint32_t sy = (uint32_t)(y - s->y);
                color = blend_argb_over_xrgb(color, s->pixels[(uint64_t)sy * s->width + sx], s->opacity);
            }
            uint8_t r = (uint8_t)((color >> 16) & 0xffu);
            uint8_t g = (uint8_t)((color >> 8) & 0xffu);
            uint8_t b = (uint8_t)(color & 0xffu);
            row[x] = osaura_display_pack_rgb(r, g, b);
        }
    }
    osaura_display_rect dirty = {(uint32_t)x0, (uint32_t)y0,
                                 (uint32_t)(x1 - x0), (uint32_t)(y1 - y0)};
    osaura_display_mark_dirty(&dirty);
    damage_reset();
    return 1;
}

uint32_t osaura_jx11_surface_count(void) {
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < OSAURA_JX11_SURFACE_MAX; ++i) if (g_surface[i].used) ++count;
    return count;
}
