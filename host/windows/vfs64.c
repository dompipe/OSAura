#ifdef _WIN64

#include "vfs64.h"
#include "../../kernel/security.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

typedef struct {
    HANDLE value;
    uint32_t subject;
    uint32_t flags;
    uint8_t used;
} vfs64_slot;

static vfs64_slot g_slots[OSAURA_WINDOWS_VFS64_HANDLE_MAX];
static char g_root[MAX_PATH];

static int has_parent_escape(const char *path) {
    const char *p = path;
    while (*p) {
        if ((p[0] == '.' && p[1] == '.' &&
             (p[2] == 0 || p[2] == '/' || p[2] == '\\')))
            return 1;
        ++p;
    }
    return 0;
}

static int resolve_path(const char *jx_path, char *out, size_t capacity) {
    if (!jx_path || !out || capacity == 0u || !g_root[0]) return -1;
    if (strchr(jx_path, ':') || has_parent_escape(jx_path)) return -2;
    while (*jx_path == '/' || *jx_path == '\\') ++jx_path;
    size_t root_n = strlen(g_root);
    size_t path_n = strlen(jx_path);
    size_t need = root_n + (path_n ? 1u : 0u) + path_n + 1u;
    if (need > capacity) return -3;
    memcpy(out, g_root, root_n);
    size_t at = root_n;
    if (path_n) out[at++] = '\\';
    for (size_t i = 0u; i < path_n; ++i)
        out[at++] = jx_path[i] == '/' ? '\\' : jx_path[i];
    out[at] = 0;
    return 0;
}

static vfs64_slot *lookup(uint32_t subject, uint32_t handle) {
    if (handle >= OSAURA_WINDOWS_VFS64_HANDLE_MAX) return 0;
    vfs64_slot *slot = &g_slots[handle];
    if (!slot->used || slot->subject != subject) return 0;
    return slot;
}

static int allocate_slot(uint32_t subject, uint32_t flags, HANDLE value, uint32_t *out) {
    if (!out) return -1;
    for (uint32_t i = 0u; i < OSAURA_WINDOWS_VFS64_HANDLE_MAX; ++i) {
        if (g_slots[i].used) continue;
        g_slots[i].value = value;
        g_slots[i].subject = subject;
        g_slots[i].flags = flags;
        g_slots[i].used = 1u;
        *out = i;
        return 0;
    }
    return -2;
}

int osaura_windows_vfs64_init(const char *host_root) {
    for (uint32_t i = 0u; i < OSAURA_WINDOWS_VFS64_HANDLE_MAX; ++i) {
        g_slots[i].value = INVALID_HANDLE_VALUE;
        g_slots[i].subject = 0u;
        g_slots[i].flags = 0u;
        g_slots[i].used = 0u;
    }
    if (!host_root || !host_root[0]) return -1;
    size_t n = strlen(host_root);
    if (n >= sizeof g_root) return -2;
    memcpy(g_root, host_root, n + 1u);
    if (!CreateDirectoryA(g_root, 0)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) return -(int)err;
    }
    return 0;
}

const char *osaura_windows_vfs64_root(void) { return g_root[0] ? g_root : 0; }

int osaura_windows_vfs64_open_as(uint32_t subject, const char *jx_path,
                                 uint32_t flags, uint32_t *handle_out) {
    if (!handle_out || !(flags & (OSAURA_WINDOWS_VFS64_READ | OSAURA_WINDOWS_VFS64_WRITE))) return -1;
    if ((flags & OSAURA_WINDOWS_VFS64_READ) &&
        !osaura_security_check(subject, OSAURA_CAP_VFS_READ)) return -2;
    if ((flags & OSAURA_WINDOWS_VFS64_WRITE) &&
        !osaura_security_check(subject, OSAURA_CAP_VFS_WRITE)) return -3;

    char path[MAX_PATH];
    int rc = resolve_path(jx_path, path, sizeof path);
    if (rc != 0) return rc;

    DWORD access = 0u;
    if (flags & OSAURA_WINDOWS_VFS64_READ) access |= GENERIC_READ;
    if (flags & OSAURA_WINDOWS_VFS64_WRITE) access |= GENERIC_WRITE;
    DWORD create_mode = (flags & OSAURA_WINDOWS_VFS64_WRITE) ? OPEN_ALWAYS : OPEN_EXISTING;
    HANDLE h = CreateFileA(path, access, FILE_SHARE_READ, 0, create_mode,
                           FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return -(int)GetLastError();
    rc = allocate_slot(subject, flags, h, handle_out);
    if (rc != 0) CloseHandle(h);
    return rc;
}

int osaura_windows_vfs64_read_as(uint32_t subject, uint32_t handle,
                                 void *buffer, uint32_t bytes, uint32_t *transferred) {
    vfs64_slot *slot = lookup(subject, handle);
    if (!slot || !(slot->flags & OSAURA_WINDOWS_VFS64_READ) || (!buffer && bytes)) return -1;
    DWORD got = 0u;
    if (!ReadFile(slot->value, buffer, bytes, &got, 0)) return -(int)GetLastError();
    if (transferred) *transferred = (uint32_t)got;
    return 0;
}

int osaura_windows_vfs64_write_as(uint32_t subject, uint32_t handle,
                                  const void *buffer, uint32_t bytes, uint32_t *transferred) {
    vfs64_slot *slot = lookup(subject, handle);
    if (!slot || !(slot->flags & OSAURA_WINDOWS_VFS64_WRITE) || (!buffer && bytes)) return -1;
    DWORD put = 0u;
    if (!WriteFile(slot->value, buffer, bytes, &put, 0)) return -(int)GetLastError();
    if (transferred) *transferred = (uint32_t)put;
    return 0;
}

int osaura_windows_vfs64_seek_as(uint32_t subject, uint32_t handle, uint64_t offset) {
    vfs64_slot *slot = lookup(subject, handle);
    if (!slot) return -1;
    LARGE_INTEGER where;
    where.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(slot->value, where, 0, FILE_BEGIN)) return -(int)GetLastError();
    return 0;
}

int osaura_windows_vfs64_stat_as(uint32_t subject, uint32_t handle,
                                 osaura_windows_vfs64_info *info) {
    vfs64_slot *slot = lookup(subject, handle);
    if (!slot || !info) return -1;
    LARGE_INTEGER size, zero, pos;
    zero.QuadPart = 0;
    if (!GetFileSizeEx(slot->value, &size)) return -(int)GetLastError();
    if (!SetFilePointerEx(slot->value, zero, &pos, FILE_CURRENT)) return -(int)GetLastError();
    info->size = (uint64_t)size.QuadPart;
    info->offset = (uint64_t)pos.QuadPart;
    info->flags = slot->flags;
    return 0;
}

int osaura_windows_vfs64_close_as(uint32_t subject, uint32_t handle) {
    vfs64_slot *slot = lookup(subject, handle);
    if (!slot) return -1;
    if (!CloseHandle(slot->value)) return -(int)GetLastError();
    slot->value = INVALID_HANDLE_VALUE;
    slot->subject = 0u;
    slot->flags = 0u;
    slot->used = 0u;
    return 0;
}

int osaura_windows_vfs64_load_as(uint32_t subject, const char *jx_path,
                                 void *buffer, uint32_t capacity, uint32_t *bytes_out) {
    uint32_t handle = OSAURA_WINDOWS_VFS64_NONE;
    int rc = osaura_windows_vfs64_open_as(subject, jx_path,
                                          OSAURA_WINDOWS_VFS64_READ, &handle);
    if (rc != 0) return rc;
    osaura_windows_vfs64_info info;
    rc = osaura_windows_vfs64_stat_as(subject, handle, &info);
    if (rc == 0 && info.size > capacity) rc = -4;
    uint32_t got = 0u;
    if (rc == 0) rc = osaura_windows_vfs64_read_as(subject, handle, buffer,
                                                    (uint32_t)info.size, &got);
    int close_rc = osaura_windows_vfs64_close_as(subject, handle);
    if (rc == 0 && close_rc != 0) rc = close_rc;
    if (rc == 0 && bytes_out) *bytes_out = got;
    return rc;
}

#endif
