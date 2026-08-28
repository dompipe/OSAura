#ifndef OSAURA_VFS_H
#define OSAURA_VFS_H

#include <stdint.h>
#include "storage.h"

#define OSAURA_VFS_HANDLE_MAX 16u
#define OSAURA_VFS_HANDLE_NONE UINT32_MAX

#define OSAURA_VFS_OPEN_READ  0x01u
#define OSAURA_VFS_OPEN_WRITE 0x02u

/* Bank 11 / opcodes 0xD8..0xDF. */
enum {
    OSAURA_VFS_HOT_OPEN  = 0u,
    OSAURA_VFS_HOT_READ  = 1u,
    OSAURA_VFS_HOT_WRITE = 2u,
    OSAURA_VFS_HOT_SEEK  = 3u,
    OSAURA_VFS_HOT_STAT  = 4u,
    OSAURA_VFS_HOT_CLOSE = 5u,
    OSAURA_VFS_HOT_FLUSH = 6u,
    OSAURA_VFS_HOT_LIST  = 7u
};

typedef struct {
    uint32_t subject;
    uint32_t handle;
    uint32_t device_id;
    uint32_t flags;
    uint32_t index;
    uint64_t offset;
    uint64_t size;
    uint32_t bytes;
    uint32_t transferred;
    void *buffer;
    const void *const_buffer;
    osaura_block_info info;
} osaura_vfs_request;

void osaura_vfs_init(void);
int osaura_vfs_hot_bind(void);
int osaura_vfs_open_device_as(uint32_t subject, uint32_t device_id, uint32_t flags, uint32_t *handle);
int osaura_vfs_read_as(uint32_t subject, uint32_t handle, void *buffer, uint32_t bytes, uint32_t *transferred);
int osaura_vfs_write_as(uint32_t subject, uint32_t handle, const void *buffer, uint32_t bytes, uint32_t *transferred);
int osaura_vfs_seek_as(uint32_t subject, uint32_t handle, uint64_t offset);
int osaura_vfs_stat_as(uint32_t subject, uint32_t handle, osaura_vfs_request *request);
int osaura_vfs_close_as(uint32_t subject, uint32_t handle);
int osaura_vfs_flush_as(uint32_t subject, uint32_t handle);
int osaura_vfs_list_as(uint32_t subject, uint32_t index, osaura_block_info *info);

/* Kernel-subject compatibility wrappers. */
int osaura_vfs_open_device(uint32_t device_id, uint32_t flags, uint32_t *handle);
int osaura_vfs_read(uint32_t handle, void *buffer, uint32_t bytes, uint32_t *transferred);
int osaura_vfs_write(uint32_t handle, const void *buffer, uint32_t bytes, uint32_t *transferred);
int osaura_vfs_seek(uint32_t handle, uint64_t offset);
int osaura_vfs_stat(uint32_t handle, osaura_vfs_request *request);
int osaura_vfs_close(uint32_t handle);
int osaura_vfs_flush(uint32_t handle);
int osaura_vfs_list(uint32_t index, osaura_block_info *info);

#endif
