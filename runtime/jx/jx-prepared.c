#include "jx-prepared.h"

#include <string.h>

#define JXL_NOP    0x00u
#define JXL_ICONST 0x01u
#define JXL_LOAD   0x02u
#define JXL_STORE  0x03u
#define JXL_POP    0x04u
#define JXL_ADD    0x05u
#define JXL_SUB    0x06u
#define JXL_MUL    0x07u
#define JXL_DIV    0x08u
#define JXL_MOD    0x09u
#define JXL_EQ     0x0au
#define JXL_NE     0x0bu
#define JXL_LT     0x0cu
#define JXL_LE     0x0du
#define JXL_GT     0x0eu
#define JXL_GE     0x0fu
#define JXL_BAND   0x10u
#define JXL_BOR    0x11u
#define JXL_BXOR   0x12u
#define JXL_SHL    0x13u
#define JXL_SHR    0x14u
#define JXL_NEG    0x15u
#define JXL_NOT    0x16u
#define JXL_JMP    0x17u
#define JXL_JZ     0x18u
#define JXL_CALL   0x19u
#define JXL_RET    0x1au
#define JXL_HALT   0x1bu

typedef struct {
    size_t return_ip;
    int64_t locals[OSAURA_JXL_LOCALS_MAX];
} jxl_frame;

typedef struct {
    const uint8_t *code;
    size_t bytes;
    size_t ip;
    int64_t stack[OSAURA_JXL_STACK_MAX];
    uint32_t sp;
    int64_t locals[OSAURA_JXL_LOCALS_MAX];
    jxl_frame frames[OSAURA_JXL_FRAMES_MAX];
    uint32_t fp;
    uint64_t budget;
    uint64_t instructions;
    uint32_t max_stack;
    uint32_t max_frames;
    int64_t last;
} jxl_vm;

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8u);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}

static uint32_t crc32_bytes(const uint8_t *data, size_t bytes) {
    uint32_t crc = 0xffffffffu;
    size_t i;
    for (i = 0; i < bytes; ++i) {
        uint32_t x = (crc ^ data[i]) & 0xffu;
        uint32_t b;
        for (b = 0; b < 8u; ++b)
            x = (x >> 1u) ^ ((x & 1u) ? 0xedb88320u : 0u);
        crc = (crc >> 8u) ^ x;
    }
    return crc ^ 0xffffffffu;
}

static int push(jxl_vm *vm, int64_t value) {
    if (vm->sp >= OSAURA_JXL_STACK_MAX) return OSAURA_JXL_ESTACK;
    vm->stack[vm->sp++] = value;
    if (vm->sp > vm->max_stack) vm->max_stack = vm->sp;
    return OSAURA_JXL_OK;
}

static int pop(jxl_vm *vm, int64_t *value) {
    if (!vm->sp) return OSAURA_JXL_ESTACK;
    *value = vm->stack[--vm->sp];
    return OSAURA_JXL_OK;
}

static int read_int(jxl_vm *vm, int64_t *out) {
    uint64_t z = 0u;
    unsigned shift = 0u;
    for (;;) {
        uint8_t b;
        uint64_t payload;
        if (vm->ip >= vm->bytes) return OSAURA_JXL_ETRUNC;
        b = vm->code[vm->ip++];
        if (!(b & 0x80u)) return OSAURA_JXL_EBYTE;
        payload = (uint64_t)(b & 0x7fu);
        if (payload == 0u && shift > 0u) break;
        if (shift >= 63u && payload) return OSAURA_JXL_EBYTE;
        z |= payload << shift;
        shift += 7u;
        if (shift > 63u) return OSAURA_JXL_EBYTE;
    }
    if (z & 1u) *out = -(int64_t)((z + 1u) >> 1u);
    else *out = (int64_t)(z >> 1u);
    return OSAURA_JXL_OK;
}

static int read_fixed(jxl_vm *vm, size_t *out) {
    uint64_t v = 0u;
    unsigned i;
    for (i = 0u; i < 5u; ++i) {
        uint8_t b;
        if (vm->ip >= vm->bytes) return OSAURA_JXL_ETRUNC;
        b = vm->code[vm->ip++];
        if (!(b & 0x80u)) return OSAURA_JXL_EBYTE;
        v |= (uint64_t)(b & 0x7fu) << (i * 7u);
    }
    if (v > (uint64_t)vm->bytes) return OSAURA_JXL_EJUMP;
    *out = (size_t)v;
    return OSAURA_JXL_OK;
}

static int binary(jxl_vm *vm, uint8_t op) {
    int64_t a, b, r = 0;
    int rc = pop(vm, &b);
    if (rc != 0) return rc;
    rc = pop(vm, &a);
    if (rc != 0) return rc;
    switch (op) {
        case JXL_ADD: r = a + b; break;
        case JXL_SUB: r = a - b; break;
        case JXL_MUL: r = a * b; break;
        case JXL_DIV: if (!b) return OSAURA_JXL_EDIV0; r = a / b; break;
        case JXL_MOD: if (!b) return OSAURA_JXL_EDIV0; r = a % b; break;
        case JXL_EQ: r = (a == b); break;
        case JXL_NE: r = (a != b); break;
        case JXL_LT: r = (a < b); break;
        case JXL_LE: r = (a <= b); break;
        case JXL_GT: r = (a > b); break;
        case JXL_GE: r = (a >= b); break;
        case JXL_BAND: r = a & b; break;
        case JXL_BOR: r = a | b; break;
        case JXL_BXOR: r = a ^ b; break;
        case JXL_SHL:
            if (b < 0 || b > 63) return OSAURA_JXL_ESHIFT;
            r = (int64_t)((uint64_t)a << (unsigned)b);
            break;
        case JXL_SHR:
            if (b < 0 || b > 63) return OSAURA_JXL_ESHIFT;
            r = a >> (unsigned)b;
            break;
        default: return OSAURA_JXL_EOP;
    }
    return push(vm, r);
}

int osaura_jxl_run(const uint8_t *code,
                   size_t code_bytes,
                   uint64_t budget,
                   osaura_jxl_result *result) {
    jxl_vm vm;
    int rc = OSAURA_JXL_OK;
    if (!code || !code_bytes || !result) return OSAURA_JXL_EINVAL;
    memset(&vm, 0, sizeof vm);
    vm.code = code;
    vm.bytes = code_bytes;
    vm.budget = budget ? budget : OSAURA_JXL_DEFAULT_BUDGET;

    while (vm.ip < vm.bytes) {
        uint8_t op;
        int64_t a, b;
        size_t target;
        if (vm.instructions++ >= vm.budget) { rc = OSAURA_JXL_EBUDGET; break; }
        op = vm.code[vm.ip++];
        if (op & 0x80u) { rc = OSAURA_JXL_EBYTE; break; }
        switch (op) {
            case JXL_NOP: break;
            case JXL_ICONST:
                rc = read_int(&vm, &a); if (!rc) rc = push(&vm, a); break;
            case JXL_LOAD:
                rc = read_int(&vm, &a);
                if (!rc && (a < 0 || a >= (int64_t)OSAURA_JXL_LOCALS_MAX)) rc = OSAURA_JXL_EINVAL;
                if (!rc) rc = push(&vm, vm.locals[(uint32_t)a]);
                break;
            case JXL_STORE:
                rc = read_int(&vm, &a);
                if (!rc && (a < 0 || a >= (int64_t)OSAURA_JXL_LOCALS_MAX)) rc = OSAURA_JXL_EINVAL;
                if (!rc) rc = pop(&vm, &b);
                if (!rc) { vm.locals[(uint32_t)a] = b; vm.last = b; }
                break;
            case JXL_POP:
                rc = pop(&vm, &a); if (!rc) vm.last = a; break;
            case JXL_ADD: case JXL_SUB: case JXL_MUL: case JXL_DIV: case JXL_MOD:
            case JXL_EQ: case JXL_NE: case JXL_LT: case JXL_LE: case JXL_GT: case JXL_GE:
            case JXL_BAND: case JXL_BOR: case JXL_BXOR: case JXL_SHL: case JXL_SHR:
                rc = binary(&vm, op); break;
            case JXL_NEG:
                rc = pop(&vm, &a); if (!rc) rc = push(&vm, -a); break;
            case JXL_NOT:
                rc = pop(&vm, &a); if (!rc) rc = push(&vm, !a); break;
            case JXL_JMP:
                rc = read_fixed(&vm, &target); if (!rc) vm.ip = target; break;
            case JXL_JZ:
                rc = read_fixed(&vm, &target);
                if (!rc) rc = pop(&vm, &a);
                if (!rc && !a) vm.ip = target;
                break;
            case JXL_CALL: {
                int64_t argc, fid;
                uint32_t i;
                int64_t args[OSAURA_JXL_LOCALS_MAX];
                rc = read_int(&vm, &argc);
                if (!rc) rc = read_int(&vm, &fid);
                if (!rc) rc = read_fixed(&vm, &target);
                (void)fid;
                if (!rc && (argc < 0 || argc > (int64_t)OSAURA_JXL_LOCALS_MAX)) rc = OSAURA_JXL_EINVAL;
                if (!rc && vm.fp >= OSAURA_JXL_FRAMES_MAX) rc = OSAURA_JXL_EFRAME;
                if (!rc && vm.sp < (uint32_t)argc) rc = OSAURA_JXL_ESTACK;
                if (rc) break;
                for (i = (uint32_t)argc; i > 0u; --i) {
                    rc = pop(&vm, &args[i - 1u]); if (rc) break;
                }
                if (rc) break;
                vm.frames[vm.fp].return_ip = vm.ip;
                memcpy(vm.frames[vm.fp].locals, vm.locals, sizeof vm.locals);
                vm.fp++;
                if (vm.fp > vm.max_frames) vm.max_frames = vm.fp;
                memset(vm.locals, 0, sizeof vm.locals);
                for (i = 0u; i < (uint32_t)argc; ++i) vm.locals[i] = args[i];
                vm.ip = target;
                break;
            }
            case JXL_RET:
                rc = pop(&vm, &a);
                if (rc) break;
                if (!vm.fp) { vm.last = a; vm.ip = vm.bytes; break; }
                vm.fp--;
                memcpy(vm.locals, vm.frames[vm.fp].locals, sizeof vm.locals);
                vm.ip = vm.frames[vm.fp].return_ip;
                vm.last = a;
                rc = push(&vm, a);
                break;
            case JXL_HALT:
                vm.ip = vm.bytes;
                break;
            default:
                rc = OSAURA_JXL_EOP;
                break;
        }
        if (rc) break;
    }

    result->result = vm.last;
    result->instructions = vm.instructions;
    result->max_stack = vm.max_stack;
    result->max_frames = vm.max_frames;
    return rc;
}

static int name_is(const uint8_t *name, uint16_t name_len, const char *literal) {
    size_t n = strlen(literal);
    return n == (size_t)name_len && memcmp(name, literal, n) == 0;
}

int osaura_jx64b_admit(const uint8_t *book,
                       size_t book_bytes,
                       int require_trust,
                       osaura_jx_book_trust_fn trust_fn,
                       void *trust_context,
                       osaura_jx64b_admission *out) {
    size_t p = 0u;
    uint32_t local_index = 0u;
    uint32_t manifest_sections = 0u;
    uint16_t major = 0u, minor = 0u;
    const uint8_t *manifest = 0, *code = 0;
    size_t manifest_bytes = 0u, code_bytes = 0u;
    int trusted = 0;

    if (!book || !book_bytes || !out) return OSAURA_JXL_EINVAL;
    memset(out, 0, sizeof *out);

    while (p + 4u <= book_bytes) {
        uint32_t sig = rd32(book + p);
        uint16_t flags, method, name_len, extra_len;
        uint32_t expected_crc, compressed, uncompressed;
        size_t name_start, data_start, data_end;
        const uint8_t *name, *data;
        if (sig == 0x02014b50u || sig == 0x06054b50u) break;
        if (sig != 0x04034b50u || p + 30u > book_bytes) return OSAURA_JXL_EBOOK;
        flags = rd16(book + p + 6u);
        method = rd16(book + p + 8u);
        expected_crc = rd32(book + p + 14u);
        compressed = rd32(book + p + 18u);
        uncompressed = rd32(book + p + 22u);
        name_len = rd16(book + p + 26u);
        extra_len = rd16(book + p + 28u);
        if (flags != 0u || method != 0u || compressed != uncompressed) return OSAURA_JXL_EBOOK;
        name_start = p + 30u;
        data_start = name_start + (size_t)name_len + (size_t)extra_len;
        if (data_start < name_start || (size_t)compressed > book_bytes - data_start) return OSAURA_JXL_ETRUNC;
        data_end = data_start + (size_t)compressed;
        name = book + name_start;
        data = book + data_start;
        if (crc32_bytes(data, compressed) != expected_crc) return OSAURA_JXL_ECRC;

        if (local_index == 0u) {
            if (!name_is(name, name_len, "JX64/header.bin") || compressed != 48u) return OSAURA_JXL_EBOOK;
            if (memcmp(data, "JX64B001", 8u) != 0) return OSAURA_JXL_EBOOK;
            major = rd16(data + 8u);
            minor = rd16(data + 10u);
            manifest_sections = rd32(data + 12u);
            if (major != 1u || minor != 0u) return OSAURA_JXL_EBOOK;
        } else if (local_index == 1u) {
            if (!name_is(name, name_len, "JX64/manifest.json")) return OSAURA_JXL_EBOOK;
            manifest = data;
            manifest_bytes = compressed;
        }
        if (name_is(name, name_len, OSAURA_JX64B_CODE_PATH)) {
            code = data;
            code_bytes = compressed;
        }
        local_index++;
        p = data_end;
    }

    if (local_index < 2u || !manifest || !manifest_bytes || !code || !code_bytes)
        return OSAURA_JXL_EBOOK;
    /* Header count describes semantic sections, excluding header+manifest. */
    if (local_index != manifest_sections + 2u) return OSAURA_JXL_EBOOK;

    if (trust_fn) trusted = trust_fn(book, book_bytes, trust_context) == 0;
    if (require_trust && !trusted) return OSAURA_JXL_ETRUST;

    out->code = code;
    out->code_bytes = code_bytes;
    out->manifest = manifest;
    out->manifest_bytes = manifest_bytes;
    out->section_count = manifest_sections;
    out->major = major;
    out->minor = minor;
    out->structurally_valid = 1u;
    out->trusted = trusted ? 1u : 0u;
    return OSAURA_JXL_OK;
}
