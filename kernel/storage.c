#include "storage.h"

#include <stddef.h>
#include <stdint.h>

#define STORAGE_NAME_COPY_MAX (OSAURA_BLOCK_NAME_MAX - 1u)

typedef struct {
    osaura_block_driver driver;
    char name[OSAURA_BLOCK_NAME_MAX];
    uint8_t used;
} osaura_block_slot;

static osaura_block_slot g_devices[OSAURA_BLOCK_DEVICE_MAX];
static uint32_t g_device_count;

static void zero_bytes(void *ptr, size_t bytes) {
    uint8_t *p = (uint8_t *)ptr;
    while (bytes--) *p++ = 0u;
}

static void copy_name(char out[OSAURA_BLOCK_NAME_MAX], const char *name) {
    size_t i = 0u;
    if (name) {
        while (name[i] && i < STORAGE_NAME_COPY_MAX) {
            out[i] = name[i];
            ++i;
        }
    }
    out[i] = 0;
}

void osaura_storage_init(void) {
    zero_bytes(g_devices, sizeof g_devices);
    g_device_count = 0u;
}

int osaura_block_register(const osaura_block_driver *driver, uint32_t *device_id) {
    if (!driver || !driver->name || !driver->read || driver->block_size == 0u ||
        driver->block_count == 0u || !(driver->capabilities & OSAURA_BLOCK_CAP_READ))
        return -1;
    if ((driver->capabilities & OSAURA_BLOCK_CAP_WRITE) && !driver->write) return -2;
    if ((driver->capabilities & OSAURA_BLOCK_CAP_FLUSH) && !driver->flush) return -3;

    for (uint32_t i = 0u; i < OSAURA_BLOCK_DEVICE_MAX; ++i) {
        if (g_devices[i].used) continue;
        g_devices[i].driver = *driver;
        copy_name(g_devices[i].name, driver->name);
        g_devices[i].driver.name = g_devices[i].name;
        g_devices[i].used = 1u;
        ++g_device_count;
        if (device_id) *device_id = i;
        return 0;
    }
    return -4;
}

uint32_t osaura_block_device_count(void) {
    return g_device_count;
}

int osaura_block_info(uint32_t device_id, osaura_block_info *info) {
    if (!info || device_id >= OSAURA_BLOCK_DEVICE_MAX || !g_devices[device_id].used)
        return -1;
    zero_bytes(info, sizeof *info);
    info->id = device_id;
    copy_name(info->name, g_devices[device_id].name);
    info->block_size = g_devices[device_id].driver.block_size;
    info->block_count = g_devices[device_id].driver.block_count;
    info->capabilities = g_devices[device_id].driver.capabilities;
    return 0;
}

static int range_ok(const osaura_block_driver *driver, uint64_t lba, uint32_t blocks) {
    if (!driver || blocks == 0u || lba >= driver->block_count) return 0;
    return (uint64_t)blocks <= driver->block_count - lba;
}

int osaura_block_read(uint32_t device_id, uint64_t lba, uint32_t blocks, void *buffer) {
    if (!buffer || device_id >= OSAURA_BLOCK_DEVICE_MAX || !g_devices[device_id].used)
        return -1;
    osaura_block_driver *driver = &g_devices[device_id].driver;
    if (!range_ok(driver, lba, blocks)) return -2;
    return driver->read(driver->context, lba, blocks, buffer);
}

int osaura_block_write(uint32_t device_id, uint64_t lba, uint32_t blocks, const void *buffer) {
    if (!buffer || device_id >= OSAURA_BLOCK_DEVICE_MAX || !g_devices[device_id].used)
        return -1;
    osaura_block_driver *driver = &g_devices[device_id].driver;
    if (!(driver->capabilities & OSAURA_BLOCK_CAP_WRITE) || !driver->write) return -2;
    if (!range_ok(driver, lba, blocks)) return -3;
    return driver->write(driver->context, lba, blocks, buffer);
}

int osaura_block_flush(uint32_t device_id) {
    if (device_id >= OSAURA_BLOCK_DEVICE_MAX || !g_devices[device_id].used) return -1;
    osaura_block_driver *driver = &g_devices[device_id].driver;
    if (!(driver->capabilities & OSAURA_BLOCK_CAP_FLUSH)) return 0;
    if (!driver->flush) return -2;
    return driver->flush(driver->context);
}
