#include "../runtime/jx/jx-prepared-live.h"

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
static size_t make_book(uint8_t*out,int value){uint8_t h[48]={0};uint8_t code[5];const uint8_t json[]="{}";size_t p=0u,c=0u;memcpy(h,"JX64B001",8u);wr16(h+8u,1u);wr32(h+12u,3u);code[c++]=0x01u;code[c++]=(uint8_t)(0x80u|((unsigned)value<<1u));code[c++]=0x80u;code[c++]=0x04u;code[c++]=0x1bu;p+=local(out+p,"JX64/header.bin",h,sizeof h);p+=local(out+p,"JX64/manifest.json",json,2u);p+=local(out+p,"CODE/program.jxl",code,c);p+=local(out+p,"META/prepared.json",json,2u);p+=local(out+p,"META/semantic.json",json,2u);return p;}
static int migration_fail(const osaura_jx64b_admission*from,const osaura_jx64b_admission*to,void*ctx){(void)from;(void)to;(void)ctx;return -77;}
static int migration_ok(const osaura_jx64b_admission*from,const osaura_jx64b_admission*to,void*ctx){int*seen=(int*)ctx;assert(to&&to->code);if(from)++*seen;return 0;}
int main(void){uint8_t a[2048],b[2048];size_t an=make_book(a,21),bn=make_book(b,42);osaura_jx_live_book live;osaura_jxl_result r;int seen=0;osaura_jx_live_book_init(&live);assert(osaura_jx_live_book_stage(&live,a,an,0,0,0)==0);assert(osaura_jx_live_book_activate(&live,migration_ok,&seen)==0);assert(live.active_generation==1u&&seen==0);assert(osaura_jxl_run(live.active.code,live.active.code_bytes,100u,&r)==0&&r.result==21);assert(osaura_jx_live_book_stage(&live,b,bn,0,0,0)==0);assert(osaura_jx_live_book_activate(&live,migration_fail,0)==-77);assert(live.active_generation==1u&&live.has_candidate==1u);assert(osaura_jxl_run(live.active.code,live.active.code_bytes,100u,&r)==0&&r.result==21);assert(osaura_jx_live_book_activate(&live,migration_ok,&seen)==0);assert(live.active_generation==2u&&live.previous_generation==1u&&seen==1);assert(osaura_jxl_run(live.active.code,live.active.code_bytes,100u,&r)==0&&r.result==42);assert(osaura_jx_live_book_rollback(&live)==0);assert(live.active_generation==1u&&live.previous_generation==2u);assert(osaura_jxl_run(live.active.code,live.active.code_bytes,100u,&r)==0&&r.result==21);puts("jx prepared live generations: ok");return 0;}
