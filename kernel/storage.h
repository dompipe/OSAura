#ifndef OSAURA_STORAGE_H
#define OSAURA_STORAGE_H

#include <stddef.h>
#include <stdint.h>
#include "hot-shadow.h"

#define OSAURA_BLOCK_DEVICE_MAX 16u
#define OSAURA_BLOCK_NAME_MAX 16u

#define OSAURA_BLOCK_CAP_READ       0x01u
#define OSAURA_BLOCK_CAP_WRITE      0x02u
#define OSAURA_BLOCK_CAP_FLUSH      0x04u
#define OSAURA_BLOCK_CAP_REMOVABLE  0x08u

typedef enum {
    OSAURA_STORAGE_READ1   = 0u,
    OSAURA_STORAGE_WRITE1  = 1u,
    OSAURA_STORAGE_READN   = 2u,
    OSAURA_STORAGE_WRITEN  = 3u,
    OSAURA_STORAGE_APPEND  = 4u,
    OSAURA_STORAGE_READAT  = 5u,
    OSAURA_STORAGE_WRITEAT = 6u,
    OSAURA_STORAGE_COMMIT  = 7u
} osaura_storage_shadow;

typedef struct {
    uint32_t device_id;
    uint64_t lba;
    uint64_t offset;
    uint32_t blocks;
    uint32_t bytes;
    void *buffer;
    const void *const_buffer;
} osaura_storage_request;

typedef int (*osaura_block_read_fn)(void *context, uint64_t lba, uint32_t blocks, void *buffer);
typedef int (*osaura_block_write_fn)(void *context, uint64_t lba, uint32_t blocks, const void *buffer);
typedef int (*osaura_block_flush_fn)(void *context);

typedef struct {
    const char *name;
    uint32_t block_size;
    uint64_t block_count;
    uint32_t capabilities;
    void *context;
    osaura_block_read_fn read;
    osaura_block_write_fn write;
    osaura_block_flush_fn flush;
} osaura_block_driver;

typedef struct {
    uint32_t id;
    char name[OSAURA_BLOCK_NAME_MAX];
    uint32_t block_size;
    uint64_t block_count;
    uint32_t capabilities;
} osaura_block_info;

void osaura_storage_init(void);
int osaura_storage_dispatch(uint8_t selector, osaura_storage_request *request);
int osaura_storage_dispatch_opcode(uint8_t opcode, osaura_storage_request *request);
const osaura_shadow_table *osaura_storage_shadows(void);
int osaura_block_register(const osaura_block_driver *driver, uint32_t *device_id);
uint32_t osaura_block_device_count(void);
int osaura_block_get_info(uint32_t device_id, osaura_block_info *info);
int osaura_block_read(uint32_t device_id, uint64_t lba, uint32_t blocks, void *buffer);
int osaura_block_write(uint32_t device_id, uint64_t lba, uint32_t blocks, const void *buffer);
int osaura_block_read_at(uint32_t device_id, uint64_t offset, uint32_t bytes, void *buffer);
int osaura_block_write_at(uint32_t device_id, uint64_t offset, uint32_t bytes, const void *buffer);
int osaura_block_flush(uint32_t device_id);

#endif
