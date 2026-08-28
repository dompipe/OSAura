#include "vfs.h"
#include "hot-shadow.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t device_id;
    uint32_t flags;
    uint64_t offset;
    uint8_t used;
} osaura_vfs_handle;

static osaura_vfs_handle g_handles[OSAURA_VFS_HANDLE_MAX];

static osaura_vfs_handle *handle_at(uint32_t handle) {
    if (handle >= OSAURA_VFS_HANDLE_MAX || !g_handles[handle].used) return 0;
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

static int raw_open(osaura_vfs_request *r) {
    if (!r || !(r->flags & OSAURA_VFS_OPEN_READ)) return -1;
    osaura_block_info info;
    if (device_size(r->device_id, &info, &r->size) != 0) return -2;
    if ((r->flags & OSAURA_VFS_OPEN_WRITE) && !(info.capabilities & OSAURA_BLOCK_CAP_WRITE)) return -3;
    for (uint32_t i = 0u; i < OSAURA_VFS_HANDLE_MAX; ++i) {
        if (g_handles[i].used) continue;
        g_handles[i].device_id = r->device_id;
        g_handles[i].flags = r->flags;
        g_handles[i].offset = 0u;
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
    osaura_vfs_handle *h = handle_at(r->handle);
    if (!h || !(h->flags & OSAURA_VFS_OPEN_READ)) return -2;
    osaura_block_info info;
    uint64_t size;
    if (device_size(h->device_id, &info, &size) != 0) return -3;
    if ((h->offset % info.block_size) != 0u || (r->bytes % info.block_size) != 0u) return -4;
    if (h->offset >= size || r->bytes > size - h->offset) return -5;
    uint64_t lba = h->offset / info.block_size;
    uint32_t blocks = r->bytes / info.block_size;
    int rc = osaura_block_read(h->device_id, lba, blocks, r->buffer);
    if (rc != 0) return rc;
    h->offset += r->bytes;
    r->offset = h->offset;
    r->transferred = r->bytes;
    return 0;
}

static int raw_write(osaura_vfs_request *r) {
    if (!r || !r->const_buffer || r->bytes == 0u) return -1;
    osaura_vfs_handle *h = handle_at(r->handle);
    if (!h || !(h->flags & OSAURA_VFS_OPEN_WRITE)) return -2;
    osaura_block_info info;
    uint64_t size;
    if (device_size(h->device_id, &info, &size) != 0) return -3;
    if ((h->offset % info.block_size) != 0u || (r->bytes % info.block_size) != 0u) return -4;
    if (h->offset >= size || r->bytes > size - h->offset) return -5;
    uint64_t lba = h->offset / info.block_size;
    uint32_t blocks = r->bytes / info.block_size;
    int rc = osaura_block_write(h->device_id, lba, blocks, r->const_buffer);
    if (rc != 0) return rc;
    h->offset += r->bytes;
    r->offset = h->offset;
    r->transferred = r->bytes;
    return 0;
}

static int raw_seek(osaura_vfs_request *r) {
    if (!r) return -1;
    osaura_vfs_handle *h = handle_at(r->handle);
    if (!h) return -2;
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
    osaura_vfs_handle *h = handle_at(r->handle);
    if (!h) return -2;
    if (device_size(h->device_id, &r->info, &r->size) != 0) return -3;
    r->device_id = h->device_id;
    r->flags = h->flags;
    r->offset = h->offset;
    return 0;
}

static int raw_close(osaura_vfs_request *r) {
    if (!r) return -1;
    osaura_vfs_handle *h = handle_at(r->handle);
    if (!h) return -2;
    h->device_id = 0u;
    h->flags = 0u;
    h->offset = 0u;
    h->used = 0u;
    return 0;
}

static int raw_flush(osaura_vfs_request *r) {
    if (!r) return -1;
    osaura_vfs_handle *h = handle_at(r->handle);
    if (!h) return -2;
    return osaura_block_flush(h->device_id);
}

static int raw_list(osaura_vfs_request *r) {
    if (!r || r->index >= osaura_block_device_count()) return -1;
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
        g_handles[i].device_id = 0u;
        g_handles[i].flags = 0u;
        g_handles[i].offset = 0u;
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

int osaura_vfs_open_device(uint32_t device_id, uint32_t flags, uint32_t *handle) {
    osaura_vfs_request r = {0}; r.device_id = device_id; r.flags = flags;
    int rc = dispatch(OSAURA_VFS_HOT_OPEN, &r); if (rc == 0 && handle) *handle = r.handle; return rc;
}
int osaura_vfs_read(uint32_t handle, void *buffer, uint32_t bytes, uint32_t *transferred) {
    osaura_vfs_request r = {0}; r.handle = handle; r.buffer = buffer; r.bytes = bytes;
    int rc = dispatch(OSAURA_VFS_HOT_READ, &r); if (rc == 0 && transferred) *transferred = r.transferred; return rc;
}
int osaura_vfs_write(uint32_t handle, const void *buffer, uint32_t bytes, uint32_t *transferred) {
    osaura_vfs_request r = {0}; r.handle = handle; r.const_buffer = buffer; r.bytes = bytes;
    int rc = dispatch(OSAURA_VFS_HOT_WRITE, &r); if (rc == 0 && transferred) *transferred = r.transferred; return rc;
}
int osaura_vfs_seek(uint32_t handle, uint64_t offset) { osaura_vfs_request r = {0}; r.handle = handle; r.offset = offset; return dispatch(OSAURA_VFS_HOT_SEEK, &r); }
int osaura_vfs_stat(uint32_t handle, osaura_vfs_request *request) { if (!request) return -1; request->handle = handle; return dispatch(OSAURA_VFS_HOT_STAT, request); }
int osaura_vfs_close(uint32_t handle) { osaura_vfs_request r = {0}; r.handle = handle; return dispatch(OSAURA_VFS_HOT_CLOSE, &r); }
int osaura_vfs_flush(uint32_t handle) { osaura_vfs_request r = {0}; r.handle = handle; return dispatch(OSAURA_VFS_HOT_FLUSH, &r); }
int osaura_vfs_list(uint32_t index, osaura_block_info *info) { osaura_vfs_request r = {0}; r.index = index; int rc = dispatch(OSAURA_VFS_HOT_LIST, &r); if (rc == 0 && info) *info = r.info; return rc; }
