#ifdef _WIN32

#include "../host/windows/prepared-win64.h"
#include "../host/windows/runtime64.h"
#include "../host/windows/vfs64.h"
#include "../kernel/security.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t crc32_bytes(const uint8_t *data, size_t bytes) {
    uint32_t crc=0xffffffffu; size_t i; unsigned b;
    for(i=0;i<bytes;++i){uint32_t x=(crc^data[i])&0xffu;for(b=0;b<8u;++b)x=(x>>1u)^((x&1u)?0xedb88320u:0u);crc=(crc>>8u)^x;}
    return crc^0xffffffffu;
}
static void wr16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8u);}
static void wr32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8u);p[2]=(uint8_t)(v>>16u);p[3]=(uint8_t)(v>>24u);}
static size_t local(uint8_t*out,const char*name,const uint8_t*data,size_t bytes){size_t n=strlen(name);memset(out,0,30u);wr32(out,0x04034b50u);wr16(out+4u,20u);wr32(out+14u,crc32_bytes(data,bytes));wr32(out+18u,(uint32_t)bytes);wr32(out+22u,(uint32_t)bytes);wr16(out+26u,(uint16_t)n);memcpy(out+30u,name,n);memcpy(out+30u+n,data,bytes);return 30u+n+bytes;}
static size_t make_book(uint8_t*out){uint8_t h[48]={0},code[]={0x01u,0xd4u,0x80u,0x04u,0x1bu};const uint8_t json[]="{}";size_t p=0u;memcpy(h,"JX64B001",8u);wr16(h+8u,1u);wr32(h+12u,3u);p+=local(out+p,"JX64/header.bin",h,sizeof h);p+=local(out+p,"JX64/manifest.json",json,2u);p+=local(out+p,"CODE/program.jxl",code,sizeof code);p+=local(out+p,"META/prepared.json",json,2u);p+=local(out+p,"META/semantic.json",json,2u);return p;}
static int trust_ok(const uint8_t*book,size_t bytes,void*ctx){(void)book;(void)bytes;(void)ctx;return 0;}
int main(void){uint8_t book[2048];size_t bytes=make_book(book);uint32_t h=OSAURA_WINDOWS_VFS64_NONE,wrote=0u;osaura_windows_prepared64_result r;CreateDirectoryA("wsjx-prepared-root",0);osaura_security_init();assert(osaura_windows_memory64_init()==0);assert(osaura_windows_vfs64_init("wsjx-prepared-root")==0);assert(osaura_windows_vfs64_open_as(OSAURA_SECURITY_KERNEL_SUBJECT,"/native.64B",OSAURA_WINDOWS_VFS64_WRITE,&h)==0);assert(osaura_windows_vfs64_write_as(OSAURA_SECURITY_KERNEL_SUBJECT,h,book,(uint32_t)bytes,&wrote)==0&&wrote==(uint32_t)bytes);assert(osaura_windows_vfs64_close_as(OSAURA_SECURITY_KERNEL_SUBJECT,h)==0);assert(osaura_windows_prepared64_run_as(OSAURA_SECURITY_JX_SUBJECT,"/native.64B",1000u,0,0,0,&r)==OSAURA_JXL_ETRUST);assert(osaura_windows_prepared64_run_as(OSAURA_SECURITY_JX_SUBJECT,"/native.64B",1000u,1,0,0,&r)==0);assert(r.execution.result==42&&r.admission.trusted==0u);assert(osaura_windows_prepared64_run_as(OSAURA_SECURITY_JX_SUBJECT,"/native.64B",1000u,0,trust_ok,0,&r)==0);assert(r.execution.result==42&&r.admission.trusted==1u);puts("WSJX64 VFS -> 64B -> JXL: PASS");return 0;}

#endif
