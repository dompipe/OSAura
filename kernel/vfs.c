#include "vfs.h"
#include "hot-shadow.h"
#include "security.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t subject;
    uint32_t device_id;
    uint32_t flags;
    uint64_t offset;
    uint8_t used;
    uint8_t busy;
    uint8_t bounce[OSAURA_VFS_BOUNCE_MAX];
} osaura_vfs_handle;

static osaura_vfs_handle g_handles[OSAURA_VFS_HANDLE_MAX];

static void copy_bytes(void *dst, const void *src, uint32_t bytes) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (bytes--) *d++ = *s++;
}

static osaura_vfs_handle *handle_at(uint32_t subject, uint32_t handle) {
    if (handle >= OSAURA_VFS_HANDLE_MAX || !g_handles[handle].used) return 0;
    if (g_handles[handle].subject != subject) return 0;
    return &g_handles[handle];
}

static int device_size(uint32_t device_id, osaura_block_info *info, uint64_t *size) {
    osaura_block_info local;
    if (!info) info = &local;
    if (osaura_block_get_info(device_id, info) != 0) return -1;
    if (info->block_size == 0u || info->block_count > UINT64_MAX / info->block_size) return -2;
    if (size) *size = info->block_count * (uint64_t)info->block_size;
    return 0;
}

static int require_right(uint32_t subject, uint64_t right) {
    return osaura_security_check(subject, right) ? 0 : -9;
}

static int raw_open(osaura_vfs_request *r) {
    if (!r || !(r->flags & OSAURA_VFS_OPEN_READ)) return -1;
    if (require_right(r->subject, OSAURA_CAP_VFS_READ) != 0) return -9;
    if ((r->flags & OSAURA_VFS_OPEN_WRITE) && require_right(r->subject, OSAURA_CAP_VFS_WRITE) != 0) return -10;

    osaura_block_info info;
    if (device_size(r->device_id, &info, &r->size) != 0) return -2;
    if ((r->flags & OSAURA_VFS_OPEN_WRITE) && !(info.capabilities & OSAURA_BLOCK_CAP_WRITE)) return -3;

    for (uint32_t i = 0u; i < OSAURA_VFS_HANDLE_MAX; ++i) {
        if (g_handles[i].used) continue;
        g_handles[i].subject = r->subject;
        g_handles[i].device_id = r->device_id;
        g_handles[i].flags = r->flags;
        g_handles[i].offset = 0u;
        g_handles[i].busy = 0u;
        g_handles[i].used = 1u;
        r->handle = i;
        r->offset = 0u;
        r->info = info;
        return 0;
    }
    return -4;
}

static int raw_read(osaura_vfs_request *r) {
    if (!r || !r->buffer || r->bytes == 0u) return -1;
    r->transferred = 0u;
    if (require_right(r->subject, OSAURA_CAP_VFS_READ) != 0) return -9;
    osaura_vfs_handle *h = handle_at(r->subject, r->handle);
    if (!h || !(h->flags & OSAURA_VFS_OPEN_READ)) return -2;
    if (h->busy) return -8;

    osaura_block_info info;
    uint64_t size;
    if (device_size(h->device_id, &info, &size) != 0) return -3;
    if (h->offset >= size) {
        r->offset = h->offset;
        return 0;
    }

    uint64_t available64 = size - h->offset;
    uint32_t remaining = r->bytes;
    if (available64 < remaining) remaining = (uint32_t)available64;

    h->busy = 1u;
    uint8_t *out = (uint8_t *)r->buffer;
    uint64_t pos = h->offset;
    uint32_t done = 0u;
    int rc = 0;

    while (remaining) {
        uint32_t in_block = (uint32_t)(pos % info.block_size);
        if (in_block == 0u && remaining >= info.block_size) {
            uint32_t blocks = remaining / info.block_size;
            uint32_t span = blocks * info.block_size;
            rc = osaura_block_read(h->device_id, pos / info.block_size, blocks, out + done);
            if (rc != 0) break;
            pos += span;
            done += span;
            remaining -= span;
            continue;
        }

        if (info.block_size > OSAURA_VFS_BOUNCE_MAX) {
            rc = -6;
            break;
        }
        rc = osaura_block_read(h->device_id, pos / info.block_size, 1u, h->bounce);
        if (rc != 0) break;
        uint32_t chunk = info.block_size - in_block;
        if (chunk > remaining) chunk = remaining;
        copy_bytes(out + done, h->bounce + in_block, chunk);
        pos += chunk;
        done += chunk;
        remaining -= chunk;
    }

    h->offset = pos;
    h->busy = 0u;
    r->offset = h->offset;
    r->transferred = done;
    return rc;
}

static int raw_write(osaura_vfs_request *r) {
    if (!r || !r->const_buffer || r->bytes == 0u) return -1;
    r->transferred = 0u;
    if (require_right(r->subject, OSAURA_CAP_VFS_WRITE) != 0) return -9;
    osaura_vfs_handle *h = handle_at(r->subject, r->handle);
    if (!h || !(h->flags & OSAURA_VFS_OPEN_WRITE)) return -2;
    if (h->busy) return -8;

    osaura_block_info info;
    uint64_t size;
    if (device_size(h->device_id, &info, &size) != 0) return -3;
    if (h->offset >= size) {
        r->offset = h->offset;
        return 0;
    }

    uint64_t available64 = size - h->offset;
    uint32_t remaining = r->bytes;
    if (available64 < remaining) remaining = (uint32_t)available64;

    h->busy = 1u;
    const uint8_t *in = (const uint8_t *)r->const_buffer;
    uint64_t pos = h->offset;
    uint32_t done = 0u;
    int rc = 0;

    while (remaining) {
        uint32_t in_block = (uint32_t)(pos % info.block_size);
        if (in_block == 0u && remaining >= info.block_size) {
            uint32_t blocks = remaining / info.block_size;
            uint32_t span = blocks * info.block_size;
            rc = osaura_block_write(h->device_id, pos / info.block_size, blocks, in + done);
            if (rc != 0) break;
            pos += span;
            done += span;
            remaining -= span;
            continue;
        }

        if (info.block_size > OSAURA_VFS_BOUNCE_MAX) {
            rc = -6;
            break;
        }
        if (!(info.capabilities & OSAURA_BLOCK_CAP_READ)) {
            rc = -7;
            break;
        }

        uint64_t lba = pos / info.block_size;
        rc = osaura_block_read(h->device_id, lba, 1u, h->bounce);
        if (rc != 0) break;
        uint32_t chunk = info.block_size - in_block;
        if (chunk > remaining) chunk = remaining;
        copy_bytes(h->bounce + in_block, in + done, chunk);
        rc = osaura_block_write(h->device_id, lba, 1u, h->bounce);
        if (rc != 0) break;
        pos += chunk;
        done += chunk;
        remaining -= chunk;
    }

    h->offset = pos;
    h->busy = 0u;
    r->offset = h->offset;
    r->transferred = done;
    return rc;
}

static int raw_seek(osaura_vfs_request *r) {
    if (!r) return -1;
    if (require_right(r->subject, OSAURA_CAP_VFS_READ) != 0) return -9;
    osaura_vfs_handle *h = handle_at(r->subject, r->handle);
    if (!h) return -2;
    if (h->busy) return -8;
    osaura_block_info info;
    uint64_t size;
    if (device_size(h->device_id, &info, &size) != 0) return -3;
    if (r->offset > size) return -4;
    h->offset = r->offset;
    r->size = size;
    return 0;
}

static int raw_stat(osaura_vfs_request *r) {
    if (!r) return -1;
    if (require_right(r->subject, OSAURA_CAP_VFS_READ) != 0) return -9;
    osaura_vfs_handle *h = handle_at(r->subject, r->handle);
    if (!h) return -2;
    if (device_size(h->device_id, &r->info, &r->size) != 0) return -3;
    r->device_id = h->device_id;
    r->flags = h->flags;
    r->offset = h->offset;
    return 0;
}

static int raw_close(osaura_vfs_request *r) {
    if (!r) return -1;
    osaura_vfs_handle *h = handle_at(r->subject, r->handle);
    if (!h) return -2;
    if (h->busy) return -8;
    h->subject = 0u;
    h->device_id = 0u;
    h->flags = 0u;
    h->offset = 0u;
    h->busy = 0u;
    h->used = 0u;
    return 0;
}

static int raw_flush(osaura_vfs_request *r) {
    if (!r) return -1;
    osaura_vfs_handle *h = handle_at(r->subject, r->handle);
    if (!h) return -2;
    if (h->busy) return -8;
    if ((h->flags & OSAURA_VFS_OPEN_WRITE) && require_right(r->subject, OSAURA_CAP_VFS_WRITE) != 0) return -9;
    return osaura_block_flush(h->device_id);
}

static int raw_list(osaura_vfs_request *r) {
    if (!r || require_right(r->subject, OSAURA_CAP_VFS_READ) != 0) return -9;
    if (r->index >= osaura_block_device_count()) return -1;
    uint32_t seen = 0u;
    for (uint32_t id = 0u; id < OSAURA_BLOCK_DEVICE_MAX; ++id) {
        osaura_block_info info;
        if (osaura_block_get_info(id, &info) != 0) continue;
        if (seen++ != r->index) continue;
        r->device_id = id;
        r->info = info;
        if (device_size(id, 0, &r->size) != 0) return -2;
        return 0;
    }
    return -3;
}

static int hot_open(void *c, void *r)  { (void)c; return raw_open((osaura_vfs_request *)r); }
static int hot_read(void *c, void *r)  { (void)c; return raw_read((osaura_vfs_request *)r); }
static int hot_write(void *c, void *r) { (void)c; return raw_write((osaura_vfs_request *)r); }
static int hot_seek(void *c, void *r)  { (void)c; return raw_seek((osaura_vfs_request *)r); }
static int hot_stat(void *c, void *r)  { (void)c; return raw_stat((osaura_vfs_request *)r); }
static int hot_close(void *c, void *r) { (void)c; return raw_close((osaura_vfs_request *)r); }
static int hot_flush(void *c, void *r) { (void)c; return raw_flush((osaura_vfs_request *)r); }
static int hot_list(void *c, void *r)  { (void)c; return raw_list((osaura_vfs_request *)r); }

void osaura_vfs_init(void) {
    for (uint32_t i = 0u; i < OSAURA_VFS_HANDLE_MAX; ++i) {
        g_handles[i].subject = 0u;
        g_handles[i].device_id = 0u;
        g_handles[i].flags = 0u;
        g_handles[i].offset = 0u;
        g_handles[i].busy = 0u;
        g_handles[i].used = 0u;
    }
}

int osaura_vfs_hot_bind(void) {
    if (osaura_hot_bind(OSAURA_HOT_BANK_VFS, OSAURA_VFS_HOT_OPEN, hot_open, 0) != 0) return -1;
    if (osaura_hot_bind(OSAURA_HOT_BANK_VFS, OSAURA_VFS_HOT_READ, hot_read, 0) != 0) return -2;
    if (osaura_hot_bind(OSAURA_HOT_BANK_VFS, OSAURA_VFS_HOT_WRITE, hot_write, 0) != 0) return -3;
    if (osaura_hot_bind(OSAURA_HOT_BANK_VFS, OSAURA_VFS_HOT_SEEK, hot_seek, 0) != 0) return -4;
    if (osaura_hot_bind(OSAURA_HOT_BANK_VFS, OSAURA_VFS_HOT_STAT, hot_stat, 0) != 0) return -5;
    if (osaura_hot_bind(OSAURA_HOT_BANK_VFS, OSAURA_VFS_HOT_CLOSE, hot_close, 0) != 0) return -6;
    if (osaura_hot_bind(OSAURA_HOT_BANK_VFS, OSAURA_VFS_HOT_FLUSH, hot_flush, 0) != 0) return -7;
    if (osaura_hot_bind(OSAURA_HOT_BANK_VFS, OSAURA_VFS_HOT_LIST, hot_list, 0) != 0) return -8;
    return 0;
}

static int dispatch(uint8_t shadow, osaura_vfs_request *r) {
    return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_VFS, shadow), r);
}

int osaura_vfs_open_device_as(uint32_t subject, uint32_t device_id, uint32_t flags, uint32_t *handle) {
    osaura_vfs_request r = {0}; r.subject = subject; r.device_id = device_id; r.flags = flags;
    int rc = dispatch(OSAURA_VFS_HOT_OPEN, &r); if (rc == 0 && handle) *handle = r.handle; return rc;
}
int osaura_vfs_read_as(uint32_t subject, uint32_t handle, void *buffer, uint32_t bytes, uint32_t *transferred) {
    osaura_vfs_request r = {0}; r.subject = subject; r.handle = handle; r.buffer = buffer; r.bytes = bytes;
    int rc = dispatch(OSAURA_VFS_HOT_READ, &r); if (transferred) *transferred = r.transferred; return rc;
}
int osaura_vfs_write_as(uint32_t subject, uint32_t handle, const void *buffer, uint32_t bytes, uint32_t *transferred) {
    osaura_vfs_request r = {0}; r.subject = subject; r.handle = handle; r.const_buffer = buffer; r.bytes = bytes;
    int rc = dispatch(OSAURA_VFS_HOT_WRITE, &r); if (transferred) *transferred = r.transferred; return rc;
}
int osaura_vfs_seek_as(uint32_t subject, uint32_t handle, uint64_t offset) { osaura_vfs_request r = {0}; r.subject = subject; r.handle = handle; r.offset = offset; return dispatch(OSAURA_VFS_HOT_SEEK, &r); }
int osaura_vfs_stat_as(uint32_t subject, uint32_t handle, osaura_vfs_request *request) { if (!request) return -1; request->subject = subject; request->handle = handle; return dispatch(OSAURA_VFS_HOT_STAT, request); }
int osaura_vfs_close_as(uint32_t subject, uint32_t handle) { osaura_vfs_request r = {0}; r.subject = subject; r.handle = handle; return dispatch(OSAURA_VFS_HOT_CLOSE, &r); }
int osaura_vfs_flush_as(uint32_t subject, uint32_t handle) { osaura_vfs_request r = {0}; r.subject = subject; r.handle = handle; return dispatch(OSAURA_VFS_HOT_FLUSH, &r); }
int osaura_vfs_list_as(uint32_t subject, uint32_t index, osaura_block_info *info) { osaura_vfs_request r = {0}; r.subject = subject; r.index = index; int rc = dispatch(OSAURA_VFS_HOT_LIST, &r); if (rc == 0 && info) *info = r.info; return rc; }

int osaura_vfs_open_device(uint32_t device_id, uint32_t flags, uint32_t *handle) { return osaura_vfs_open_device_as(OSAURA_SECURITY_KERNEL_SUBJECT, device_id, flags, handle); }
int osaura_vfs_read(uint32_t handle, void *buffer, uint32_t bytes, uint32_t *transferred) { return osaura_vfs_read_as(OSAURA_SECURITY_KERNEL_SUBJECT, handle, buffer, bytes, transferred); }
int osaura_vfs_write(uint32_t handle, const void *buffer, uint32_t bytes, uint32_t *transferred) { return osaura_vfs_write_as(OSAURA_SECURITY_KERNEL_SUBJECT, handle, buffer, bytes, transferred); }
int osaura_vfs_seek(uint32_t handle, uint64_t offset) { return osaura_vfs_seek_as(OSAURA_SECURITY_KERNEL_SUBJECT, handle, offset); }
int osaura_vfs_stat(uint32_t handle, osaura_vfs_request *request) { return osaura_vfs_stat_as(OSAURA_SECURITY_KERNEL_SUBJECT, handle, request); }
int osaura_vfs_close(uint32_t handle) { return osaura_vfs_close_as(OSAURA_SECURITY_KERNEL_SUBJECT, handle); }
int osaura_vfs_flush(uint32_t handle) { return osaura_vfs_flush_as(OSAURA_SECURITY_KERNEL_SUBJECT, handle); }
int osaura_vfs_list(uint32_t index, osaura_block_info *info) { return osaura_vfs_list_as(OSAURA_SECURITY_KERNEL_SUBJECT, index, info); }
