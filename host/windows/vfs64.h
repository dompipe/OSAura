#ifndef OSAURA_WINDOWS_VFS64_H
#define OSAURA_WINDOWS_VFS64_H

#include <stdint.h>

#define OSAURA_WINDOWS_VFS64_HANDLE_MAX 32u
#define OSAURA_WINDOWS_VFS64_NONE UINT32_MAX
#define OSAURA_WINDOWS_VFS64_READ  0x01u
#define OSAURA_WINDOWS_VFS64_WRITE 0x02u

typedef struct {
    uint64_t size;
    uint64_t offset;
    uint32_t flags;
} osaura_windows_vfs64_info;

int osaura_windows_vfs64_init(const char *host_root);
const char *osaura_windows_vfs64_root(void);
int osaura_windows_vfs64_open_as(uint32_t subject, const char *jx_path,
                                 uint32_t flags, uint32_t *handle_out);
int osaura_windows_vfs64_read_as(uint32_t subject, uint32_t handle,
                                 void *buffer, uint32_t bytes, uint32_t *transferred);
int osaura_windows_vfs64_write_as(uint32_t subject, uint32_t handle,
                                  const void *buffer, uint32_t bytes, uint32_t *transferred);
int osaura_windows_vfs64_seek_as(uint32_t subject, uint32_t handle, uint64_t offset);
int osaura_windows_vfs64_stat_as(uint32_t subject, uint32_t handle,
                                 osaura_windows_vfs64_info *info);
int osaura_windows_vfs64_close_as(uint32_t subject, uint32_t handle);
int osaura_windows_vfs64_load_as(uint32_t subject, const char *jx_path,
                                 void *buffer, uint32_t capacity, uint32_t *bytes_out);

#endif
