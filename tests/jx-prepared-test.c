#include "../runtime/jx/jx-prepared.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static size_t put_int(uint8_t *p, int64_t value) {
    uint64_t z = value < 0 ? (((uint64_t)(-value) << 1u) - 1u) : ((uint64_t)value << 1u);
    size_t n = 0u;
    do { p[n++] = (uint8_t)(0x80u | (z & 0x7fu)); z >>= 7u; } while (z);
    p[n++] = 0x80u;
    return n;
}

static void put_fixed(uint8_t *p, size_t value) {
    unsigned i;
    for (i = 0u; i < 5u; ++i) { p[i] = (uint8_t)(0x80u | (value & 0x7fu)); value >>= 7u; }
}

static uint32_t crc32_bytes(const uint8_t *data, size_t bytes) {
    uint32_t crc = 0xffffffffu;
    size_t i;
    for (i = 0; i < bytes; ++i) {
        uint32_t x = (crc ^ data[i]) & 0xffu;
        unsigned b;
        for (b = 0u; b < 8u; ++b) x = (x >> 1u) ^ ((x & 1u) ? 0xedb88320u : 0u);
        crc = (crc >> 8u) ^ x;
    }
    return crc ^ 0xffffffffu;
}

static void wr16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8u); }
static void wr32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8u); p[2]=(uint8_t)(v>>16u); p[3]=(uint8_t)(v>>24u); }

static size_t zip_local(uint8_t *out, const char *name, const uint8_t *data, size_t bytes) {
    size_t n = strlen(name);
    memset(out, 0, 30u);
    wr32(out, 0x04034b50u); wr16(out+4u, 20u); wr32(out+14u, crc32_bytes(data, bytes));
    wr32(out+18u, (uint32_t)bytes); wr32(out+22u, (uint32_t)bytes);
    wr16(out+26u, (uint16_t)n);
    memcpy(out+30u, name, n); memcpy(out+30u+n, data, bytes);
    return 30u+n+bytes;
}

static int trust_ok(const uint8_t *book, size_t bytes, void *ctx) {
    (void)book; (void)bytes; (void)ctx; return 0;
}

static void test_vm(void) {
    uint8_t code[128]; size_t p=0u, call_target_pos, function_target;
    osaura_jxl_result r;
    code[p++]=0x01u; p+=put_int(code+p,20);
    code[p++]=0x01u; p+=put_int(code+p,22);
    code[p++]=0x19u; p+=put_int(code+p,2); p+=put_int(code+p,0);
    call_target_pos=p; p+=5u;
    code[p++]=0x04u; code[p++]=0x1bu;
    function_target=p;
    code[p++]=0x02u; p+=put_int(code+p,0);
    code[p++]=0x02u; p+=put_int(code+p,1);
    code[p++]=0x05u; code[p++]=0x1au;
    put_fixed(code+call_target_pos,function_target);
    assert(osaura_jxl_run(code,p,1000u,&r)==OSAURA_JXL_OK);
    assert(r.result==42); assert(r.instructions>0u); assert(r.max_frames==1u);

    { uint8_t bad[]={0x80u}; assert(osaura_jxl_run(bad,sizeof bad,10u,&r)==OSAURA_JXL_EBYTE); }
    { uint8_t div0[32]; size_t q=0u; div0[q++]=0x01u; q+=put_int(div0+q,1); div0[q++]=0x01u; q+=put_int(div0+q,0); div0[q++]=0x08u; div0[q++]=0x1bu; assert(osaura_jxl_run(div0,q,20u,&r)==OSAURA_JXL_EDIV0); }
    { uint8_t loop[6]={0x17u,0x80u,0x80u,0x80u,0x80u,0x80u}; assert(osaura_jxl_run(loop,sizeof loop,8u,&r)==OSAURA_JXL_EBUDGET); }
}

static void test_book(void) {
    uint8_t book[4096], header[48], code[16];
    const uint8_t manifest[]="{}", semantic[]="{}", prepared[]="{}";
    size_t bp=0u, cp=0u;
    osaura_jx64b_admission a;
    memset(header,0,sizeof header); memcpy(header,"JX64B001",8u); wr16(header+8u,1u); wr16(header+10u,0u); wr32(header+12u,3u);
    code[cp++]=0x01u; cp+=put_int(code+cp,42); code[cp++]=0x04u; code[cp++]=0x1bu;
    bp+=zip_local(book+bp,"JX64/header.bin",header,sizeof header);
    bp+=zip_local(book+bp,"JX64/manifest.json",manifest,sizeof manifest-1u);
    bp+=zip_local(book+bp,"CODE/program.jxl",code,cp);
    bp+=zip_local(book+bp,"META/prepared.json",prepared,sizeof prepared-1u);
    bp+=zip_local(book+bp,"META/semantic.json",semantic,sizeof semantic-1u);
    assert(osaura_jx64b_admit(book,bp,0,0,0,&a)==OSAURA_JXL_OK);
    assert(a.structurally_valid==1u && a.trusted==0u && a.section_count==3u);
    assert(a.code_bytes==cp && memcmp(a.code,code,cp)==0);
    assert(osaura_jx64b_admit(book,bp,1,0,0,&a)==OSAURA_JXL_ETRUST);
    assert(osaura_jx64b_admit(book,bp,1,trust_ok,0,&a)==OSAURA_JXL_OK && a.trusted==1u);
    book[40u]^=1u;
    assert(osaura_jx64b_admit(book,bp,0,0,0,&a)==OSAURA_JXL_ECRC);
}

int main(void) {
    test_vm(); test_book();
    puts("jx prepared VM/admission: ok");
    return 0;
}
