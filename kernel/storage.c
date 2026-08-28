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

static void zero_bytes(void *ptr, size_t bytes) { uint8_t *p = (uint8_t *)ptr; while (bytes--) *p++ = 0u; }
static void copy_name(char out[OSAURA_BLOCK_NAME_MAX], const char *name) {
    size_t i = 0u; if (name) while (name[i] && i < STORAGE_NAME_COPY_MAX) { out[i] = name[i]; ++i; } out[i] = 0;
}
static osaura_block_driver *request_driver(osaura_storage_request *request) {
    if (!request || request->device_id >= OSAURA_BLOCK_DEVICE_MAX || !g_devices[request->device_id].used) return 0;
    return &g_devices[request->device_id].driver;
}
static int range_ok(const osaura_block_driver *driver, uint64_t lba, uint32_t blocks) {
    if (!driver || blocks == 0u || lba >= driver->block_count) return 0;
    return (uint64_t)blocks <= driver->block_count - lba;
}
static int byte_request(const osaura_block_driver *driver, const osaura_storage_request *r, uint64_t *lba, uint32_t *blocks) {
    if (!driver || !r || !driver->block_size || !r->bytes) return -1;
    if ((r->offset % driver->block_size) != 0u || (r->bytes % driver->block_size) != 0u) return -2;
    uint64_t b = (uint64_t)r->bytes / driver->block_size;
    if (!b || b > UINT32_MAX) return -3;
    uint64_t start = r->offset / driver->block_size;
    if (!range_ok(driver, start, (uint32_t)b)) return -4;
    *lba = start; *blocks = (uint32_t)b; return 0;
}

static int shadow_read1(void *c, void *o) { (void)c; osaura_storage_request *r=o; osaura_block_driver *d=request_driver(r); if(!d||!r->buffer)return -1; if(!range_ok(d,r->lba,1u))return -2; return d->read(d->context,r->lba,1u,r->buffer); }
static int shadow_write1(void *c, void *o) { (void)c; osaura_storage_request *r=o; osaura_block_driver *d=request_driver(r); if(!d||!r->const_buffer)return -1; if(!(d->capabilities&OSAURA_BLOCK_CAP_WRITE)||!d->write)return -2; if(!range_ok(d,r->lba,1u))return -3; return d->write(d->context,r->lba,1u,r->const_buffer); }
static int shadow_readn(void *c, void *o) { (void)c; osaura_storage_request *r=o; osaura_block_driver *d=request_driver(r); if(!d||!r->buffer)return -1; if(!range_ok(d,r->lba,r->blocks))return -2; return d->read(d->context,r->lba,r->blocks,r->buffer); }
static int shadow_writen(void *c, void *o) { (void)c; osaura_storage_request *r=o; osaura_block_driver *d=request_driver(r); if(!d||!r->const_buffer)return -1; if(!(d->capabilities&OSAURA_BLOCK_CAP_WRITE)||!d->write)return -2; if(!range_ok(d,r->lba,r->blocks))return -3; return d->write(d->context,r->lba,r->blocks,r->const_buffer); }
static int shadow_append_unbound(void *c, void *o) { (void)c; (void)o; return -8; }
static int shadow_readat(void *c, void *o) {
    (void)c; osaura_storage_request *r=o; osaura_block_driver *d=request_driver(r); uint64_t lba; uint32_t blocks;
    if(!d||!r->buffer)return -1; int rc=byte_request(d,r,&lba,&blocks); if(rc)return rc; return d->read(d->context,lba,blocks,r->buffer);
}
static int shadow_writeat(void *c, void *o) {
    (void)c; osaura_storage_request *r=o; osaura_block_driver *d=request_driver(r); uint64_t lba; uint32_t blocks;
    if(!d||!r->const_buffer)return -1; if(!(d->capabilities&OSAURA_BLOCK_CAP_WRITE)||!d->write)return -2;
    int rc=byte_request(d,r,&lba,&blocks); if(rc)return rc; return d->write(d->context,lba,blocks,r->const_buffer);
}
static int shadow_commit(void *c, void *o) { (void)c; osaura_storage_request *r=o; osaura_block_driver *d=request_driver(r); if(!d)return -1; if(!(d->capabilities&OSAURA_BLOCK_CAP_FLUSH))return 0; if(!d->flush)return -2; return d->flush(d->context); }

void osaura_storage_init(void) {
    zero_bytes(g_devices,sizeof g_devices); g_device_count=0u;
    (void)osaura_hot_bind(OSAURA_HOT_BANK_STORAGE,OSAURA_STORAGE_READ1,shadow_read1,0);
    (void)osaura_hot_bind(OSAURA_HOT_BANK_STORAGE,OSAURA_STORAGE_WRITE1,shadow_write1,0);
    (void)osaura_hot_bind(OSAURA_HOT_BANK_STORAGE,OSAURA_STORAGE_READN,shadow_readn,0);
    (void)osaura_hot_bind(OSAURA_HOT_BANK_STORAGE,OSAURA_STORAGE_WRITEN,shadow_writen,0);
    (void)osaura_hot_bind(OSAURA_HOT_BANK_STORAGE,OSAURA_STORAGE_APPEND,shadow_append_unbound,0);
    (void)osaura_hot_bind(OSAURA_HOT_BANK_STORAGE,OSAURA_STORAGE_READAT,shadow_readat,0);
    (void)osaura_hot_bind(OSAURA_HOT_BANK_STORAGE,OSAURA_STORAGE_WRITEAT,shadow_writeat,0);
    (void)osaura_hot_bind(OSAURA_HOT_BANK_STORAGE,OSAURA_STORAGE_COMMIT,shadow_commit,0);
}

int osaura_storage_dispatch(uint8_t selector, osaura_storage_request *request) { return osaura_hot_dispatch(OSAURA_HOT_BANK_STORAGE,selector,request); }
int osaura_storage_dispatch_opcode(uint8_t opcode, osaura_storage_request *request) { if(osaura_hot_bank(opcode)!=OSAURA_HOT_BANK_STORAGE)return -1; return osaura_hot_dispatch_opcode(opcode,request); }
const osaura_shadow_table *osaura_storage_shadows(void) { return osaura_hot_table(); }

int osaura_block_register(const osaura_block_driver *driver, uint32_t *device_id) {
    if(!driver||!driver->name||!driver->read||!driver->block_size||!driver->block_count||!(driver->capabilities&OSAURA_BLOCK_CAP_READ))return -1;
    if((driver->capabilities&OSAURA_BLOCK_CAP_WRITE)&&!driver->write)return -2;
    if((driver->capabilities&OSAURA_BLOCK_CAP_FLUSH)&&!driver->flush)return -3;
    for(uint32_t i=0u;i<OSAURA_BLOCK_DEVICE_MAX;++i){ if(g_devices[i].used)continue; g_devices[i].driver=*driver; copy_name(g_devices[i].name,driver->name); g_devices[i].driver.name=g_devices[i].name; g_devices[i].used=1u; ++g_device_count; if(device_id)*device_id=i; return 0; } return -4;
}
uint32_t osaura_block_device_count(void) { return g_device_count; }
int osaura_block_get_info(uint32_t device_id, osaura_block_info *info) { if(!info||device_id>=OSAURA_BLOCK_DEVICE_MAX||!g_devices[device_id].used)return -1; zero_bytes(info,sizeof *info); info->id=device_id; copy_name(info->name,g_devices[device_id].name); info->block_size=g_devices[device_id].driver.block_size; info->block_count=g_devices[device_id].driver.block_count; info->capabilities=g_devices[device_id].driver.capabilities; return 0; }
int osaura_block_read(uint32_t device_id,uint64_t lba,uint32_t blocks,void *buffer){ osaura_storage_request r={device_id,lba,0u,blocks,0u,buffer,0}; return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_STORAGE,blocks==1u?OSAURA_STORAGE_READ1:OSAURA_STORAGE_READN),&r); }
int osaura_block_write(uint32_t device_id,uint64_t lba,uint32_t blocks,const void *buffer){ osaura_storage_request r={device_id,lba,0u,blocks,0u,0,buffer}; return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_STORAGE,blocks==1u?OSAURA_STORAGE_WRITE1:OSAURA_STORAGE_WRITEN),&r); }
int osaura_block_read_at(uint32_t device_id,uint64_t offset,uint32_t bytes,void *buffer){ osaura_storage_request r={device_id,0u,offset,0u,bytes,buffer,0}; return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_STORAGE,OSAURA_STORAGE_READAT),&r); }
int osaura_block_write_at(uint32_t device_id,uint64_t offset,uint32_t bytes,const void *buffer){ osaura_storage_request r={device_id,0u,offset,0u,bytes,0,buffer}; return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_STORAGE,OSAURA_STORAGE_WRITEAT),&r); }
int osaura_block_flush(uint32_t device_id){ osaura_storage_request r={device_id,0u,0u,0u,0u,0,0}; return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_STORAGE,OSAURA_STORAGE_COMMIT),&r); }
