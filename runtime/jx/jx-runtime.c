#include "jx-runtime.h"

#include <stddef.h>
#include <stdint.h>

#define JX_SYSTEM_ESCAPE 0x7fu
#define JX_SYSTEM_BUS 0x00u
#define JX_BUS_TICK 0x01u
#define JX_BUS_COLLECT 0x02u
#define JX_SYSTEM_BYTES 3u
#define COM1 0x3f8u

#define JX64_LOCAL_SIGNATURE 0x04034b50u
#define JX64_CENTRAL_SIGNATURE 0x02014b50u
#define JX64_EOCD_SIGNATURE 0x06054b50u
#define JX64_LOCAL_HEADER_BYTES 30u
#define JX64_IDENTITY_BYTES 48u
#define JX64_MAX_BOOK_BYTES (64ull << 20)
#define JX64_MAX_SECTION_BYTES (256u << 20)
#define JX64_UTF8_FLAG 0x0800u

#define JX_CHANNEL_BUS_VERSION 1u
#define JX_CHANNEL_BUS_MAX_CHANNELS 64u
#define JX_CHANNEL_BUS_MAX_ENDPOINTS 64u
#define JX_CHANNEL_BUS_QUEUE_DEPTH 64u
#define JX_CHANNEL_DIR_IN 1u
#define JX_CHANNEL_DIR_OUT 2u
#define JX_CHANNEL_DIR_INOUT 3u
#define JX_RUNTIME_ENDPOINT 1u
#define JX_PROGRAM_A 0x80000001u
#define JX_PROGRAM_B 0x80000002u
#define JX_RUNTIME_CHANNEL 1u
#define JX_MESSAGE_BOOT 1u
#define JX_MESSAGE_TICK 2u

#define JX_BAG_SLOTS 7u
#define JX_BAG_HEARTBEAT 0u
#define JX_BAG_BUS_TICKS 1u
#define JX_BAG_BUS_COLLECTS 2u
#define JX_BAG_CHANNEL_MESSAGES 3u
#define JX_BAG_CHANNEL_DELIVERIES 4u
#define JX_BAG_CHANNEL_SWITCHES 5u
#define JX_BAG_LAST_MESSAGE_TYPE 6u

typedef struct {
    uint32_t state[8];
    uint64_t bits;
    uint8_t block[64];
    size_t block_used;
} sha256_ctx;

typedef struct {
    const uint8_t *name;
    uint16_t name_bytes;
    const uint8_t *data;
    uint32_t data_bytes;
    uint32_t crc32;
    size_t next_offset;
} jx64_entry;

typedef struct {
    const uint8_t *applied_bus;
    size_t applied_bus_bytes;
    const uint8_t *bag_schema;
    size_t bag_schema_bytes;
    uint32_t sections;
    uint16_t major;
    uint16_t minor;
} jx64_book_view;

typedef int (*jx_channel_receive_fn)(uint16_t channel_id,
                                     uint32_t message_type,
                                     void *payload,
                                     void *context);

typedef struct {
    uint16_t channel_id;
    uint8_t direction;
    uint8_t in_use;
} jx_channel_binding;

typedef struct {
    uint32_t endpoint_id;
    jx_channel_receive_fn receive;
    void *context;
    jx_channel_binding bindings[JX_CHANNEL_BUS_MAX_CHANNELS];
    size_t binding_count;
    uint8_t in_use;
} jx_channel_endpoint;

typedef struct {
    uint16_t channel_id;
    uint32_t message_type;
    void *payload;
    uint32_t source_endpoint;
} jx_channel_message;

typedef struct {
    uint8_t version;
    uint8_t paused;
    uint32_t active_program_endpoint;
    jx_channel_endpoint endpoints[JX_CHANNEL_BUS_MAX_ENDPOINTS];
    size_t endpoint_count;
    jx_channel_message queue[JX_CHANNEL_BUS_QUEUE_DEPTH];
    size_t queue_head;
    size_t queue_count;
} jx_channel_bus;

typedef struct {
    volatile uint64_t hot[JX_BAG_SLOTS];
    uint64_t canonical[JX_BAG_SLOTS];
    volatile uint64_t revision;
    volatile uint64_t checkpoints;
    uint8_t dirty;
} jx_record_bag;

typedef struct {
    uint32_t endpoint_id;
    uint64_t deliveries;
} jx_program_probe;

static volatile uint8_t g_active;
static volatile uint8_t g_book_loaded;
static volatile uint8_t g_bus_ready;
static volatile uint64_t g_errors;
static uint8_t g_announced;
static jx64_book_view g_book;
static jx_record_bag g_bag;
static jx_channel_bus g_bus;
static jx_program_probe g_program_a;
static jx_program_probe g_program_b;

static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void serial_char(char c) {
    if (c == '\n') serial_char('\r');
    while (!(in8(COM1 + 5u) & 0x20u)) __asm__ volatile("pause");
    out8(COM1, (uint8_t)c);
}

static void serial_text(const char *text) {
    while (*text) serial_char(*text++);
}

static void zero_bytes(void *target, size_t bytes) {
    uint8_t *out = (uint8_t *)target;
    for (size_t i = 0; i < bytes; ++i) out[i] = 0u;
}

static int bytes_equal(const uint8_t *a, const uint8_t *b, size_t bytes) {
    if (!a || !b) return 0;
    for (size_t i = 0; i < bytes; ++i)
        if (a[i] != b[i]) return 0;
    return 1;
}

static size_t text_length(const char *text) {
    size_t n = 0;
    while (text && text[n]) ++n;
    return n;
}

static int entry_name_equal(const jx64_entry *entry, const char *name) {
    size_t bytes = text_length(name);
    return entry && bytes == entry->name_bytes &&
           bytes_equal(entry->name, (const uint8_t *)name, bytes);
}

static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t rotr32(uint32_t x, uint32_t shift) {
    return (x >> shift) | (x << (32u - shift));
}

static void sha256_transform(sha256_ctx *ctx, const uint8_t block[64]) {
    static const uint32_t k[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    uint32_t w[64];
    for (uint32_t i = 0; i < 16u; ++i) {
        uint32_t j = i * 4u;
        w[i] = ((uint32_t)block[j] << 24) |
               ((uint32_t)block[j + 1u] << 16) |
               ((uint32_t)block[j + 2u] << 8) |
               (uint32_t)block[j + 3u];
    }
    for (uint32_t i = 16u; i < 64u; ++i) {
        uint32_t s0 = rotr32(w[i - 15u], 7u) ^ rotr32(w[i - 15u], 18u) ^ (w[i - 15u] >> 3u);
        uint32_t s1 = rotr32(w[i - 2u], 17u) ^ rotr32(w[i - 2u], 19u) ^ (w[i - 2u] >> 10u);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (uint32_t i = 0; i < 64u; ++i) {
        uint32_t s1 = rotr32(e, 6u) ^ rotr32(e, 11u) ^ rotr32(e, 25u);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = rotr32(a, 2u) ^ rotr32(a, 13u) ^ rotr32(a, 22u);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx *ctx) {
    static const uint32_t init[8] = {
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    };
    for (uint32_t i = 0; i < 8u; ++i) ctx->state[i] = init[i];
    ctx->bits = 0;
    ctx->block_used = 0;
}

static void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t bytes) {
    if (!ctx || (!data && bytes)) return;
    ctx->bits += (uint64_t)bytes * 8u;
    while (bytes) {
        size_t room = sizeof ctx->block - ctx->block_used;
        size_t take = bytes < room ? bytes : room;
        for (size_t i = 0; i < take; ++i) ctx->block[ctx->block_used + i] = data[i];
        ctx->block_used += take;
        data += take;
        bytes -= take;
        if (ctx->block_used == sizeof ctx->block) {
            sha256_transform(ctx, ctx->block);
            ctx->block_used = 0;
        }
    }
}

static void sha256_final(sha256_ctx *ctx, uint8_t digest[32]) {
    uint64_t bits = ctx->bits;
    ctx->block[ctx->block_used++] = 0x80u;
    if (ctx->block_used > 56u) {
        while (ctx->block_used < 64u) ctx->block[ctx->block_used++] = 0u;
        sha256_transform(ctx, ctx->block);
        ctx->block_used = 0;
    }
    while (ctx->block_used < 56u) ctx->block[ctx->block_used++] = 0u;
    for (uint32_t i = 0; i < 8u; ++i)
        ctx->block[63u - i] = (uint8_t)(bits >> (i * 8u));
    sha256_transform(ctx, ctx->block);
    for (uint32_t i = 0; i < 8u; ++i) {
        digest[i * 4u] = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4u + 1u] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4u + 2u] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4u + 3u] = (uint8_t)ctx->state[i];
    }
}

static void sha256_bytes(const uint8_t *data, size_t bytes, uint8_t digest[32]) {
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, bytes);
    sha256_final(&ctx, digest);
}

static uint32_t crc32_bytes(const uint8_t *data, size_t bytes) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < bytes; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

static int portable_section_name(const uint8_t *name, size_t bytes) {
    if (!name || !bytes || bytes > 1024u || name[0] == '/') return 0;
    for (size_t i = 0; i < bytes; ++i) {
        uint8_t c = name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '/' || c == '.' || c == '_' || c == '-')
            continue;
        return 0;
    }
    return 1;
}

static int parse_local_entry(const uint8_t *book, size_t book_bytes, size_t offset, jx64_entry *out) {
    if (!book || !out || offset > book_bytes || book_bytes - offset < JX64_LOCAL_HEADER_BYTES) return -1;
    const uint8_t *p = book + offset;
    if (read_le32(p) != JX64_LOCAL_SIGNATURE) return -2;
    uint16_t flags = read_le16(p + 6u);
    uint16_t method = read_le16(p + 8u);
    uint32_t crc = read_le32(p + 14u);
    uint32_t compressed = read_le32(p + 18u);
    uint32_t uncompressed = read_le32(p + 22u);
    uint16_t name_bytes = read_le16(p + 26u);
    uint16_t extra_bytes = read_le16(p + 28u);
    if (flags & (uint16_t)~JX64_UTF8_FLAG) return -3;
    if (method != 0u || compressed != uncompressed || uncompressed > JX64_MAX_SECTION_BYTES) return -4;
    size_t data_offset = offset + JX64_LOCAL_HEADER_BYTES + (size_t)name_bytes + (size_t)extra_bytes;
    if (data_offset < offset || data_offset > book_bytes || uncompressed > book_bytes - data_offset) return -5;
    const uint8_t *name = p + JX64_LOCAL_HEADER_BYTES;
    if (!portable_section_name(name, name_bytes)) return -6;
    const uint8_t *data = book + data_offset;
    if (crc32_bytes(data, uncompressed) != crc) return -7;
    out->name = name;
    out->name_bytes = name_bytes;
    out->data = data;
    out->data_bytes = uncompressed;
    out->crc32 = crc;
    out->next_offset = data_offset + uncompressed;
    return 0;
}

static const uint8_t *find_literal(const uint8_t *data, size_t bytes, const char *literal) {
    size_t n = text_length(literal);
    if (!data || !n || n > bytes) return NULL;
    for (size_t i = 0; i <= bytes - n; ++i)
        if (bytes_equal(data + i, (const uint8_t *)literal, n)) return data + i;
    return NULL;
}

static int hex_nibble(uint8_t c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return 10 + (int)(c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (int)(c - 'A');
    return -1;
}

static int parse_hex_digest(const uint8_t *text, const uint8_t *end, uint8_t digest[32]) {
    if (!text || !end || end < text || (size_t)(end - text) < 64u) return 0;
    for (uint32_t i = 0; i < 32u; ++i) {
        int hi = hex_nibble(text[i * 2u]);
        int lo = hex_nibble(text[i * 2u + 1u]);
        if (hi < 0 || lo < 0) return 0;
        digest[i] = (uint8_t)((hi << 4) | lo);
    }
    return 1;
}

static int parse_decimal(const uint8_t **cursor, const uint8_t *end, uint32_t *value) {
    const uint8_t *p = *cursor;
    uint64_t n = 0;
    if (!p || p >= end || *p < '0' || *p > '9') return 0;
    while (p < end && *p >= '0' && *p <= '9') {
        n = n * 10u + (uint64_t)(*p - '0');
        if (n > 0xffffffffu) return 0;
        ++p;
    }
    *cursor = p;
    *value = (uint32_t)n;
    return 1;
}

static int require_literal(const uint8_t **cursor, const uint8_t *end, const char *literal) {
    size_t n = text_length(literal);
    if (!cursor || !*cursor || !end || (size_t)(end - *cursor) < n) return 0;
    if (!bytes_equal(*cursor, (const uint8_t *)literal, n)) return 0;
    *cursor += n;
    return 1;
}

static int parse_manifest_sections(const uint8_t *manifest,
                                   size_t manifest_bytes,
                                   const uint8_t *book,
                                   size_t book_bytes,
                                   size_t first_section_offset,
                                   uint32_t expected_sections,
                                   jx64_book_view *view) {
    static const char sections_key[] = "\"sections\":[";
    static const char content_key[] = "\"content_sha256\":\"";
    const uint8_t *end = manifest + manifest_bytes;
    const uint8_t *p = find_literal(manifest, manifest_bytes, sections_key);
    if (!p) return -1;
    p += sizeof sections_key - 1u;

    const uint8_t *content = find_literal(manifest, manifest_bytes, content_key);
    if (!content) return -2;
    content += sizeof content_key - 1u;
    uint8_t expected_content[32];
    if (!parse_hex_digest(content, end, expected_content)) return -3;

    sha256_ctx canonical;
    sha256_init(&canonical);
    size_t zip_offset = first_section_offset;

    for (uint32_t row = 0; row < expected_sections; ++row) {
        if (!require_literal(&p, end, "{\"bytes\":")) return -4;
        uint32_t manifest_size = 0;
        if (!parse_decimal(&p, end, &manifest_size)) return -5;
        if (!require_literal(&p, end, ",\"name\":\"")) return -6;
        const uint8_t *name = p;
        while (p < end && *p != '"') {
            if (*p == '\\') return -7;
            ++p;
        }
        if (p >= end) return -8;
        size_t name_bytes = (size_t)(p - name);
        if (!portable_section_name(name, name_bytes)) return -9;
        ++p;
        if (!require_literal(&p, end, ",\"sha256\":\"")) return -10;
        uint8_t manifest_digest[32];
        if (!parse_hex_digest(p, end, manifest_digest)) return -11;
        p += 64u;
        if (!require_literal(&p, end, "\"}")) return -12;
        if (row + 1u < expected_sections) {
            if (!require_literal(&p, end, ",")) return -13;
        }

        jx64_entry entry;
        if (parse_local_entry(book, book_bytes, zip_offset, &entry) != 0) return -14;
        if (entry.name_bytes != name_bytes || !bytes_equal(entry.name, name, name_bytes)) return -15;
        if (entry.data_bytes != manifest_size) return -16;
        uint8_t actual_digest[32];
        sha256_bytes(entry.data, entry.data_bytes, actual_digest);
        if (!bytes_equal(actual_digest, manifest_digest, sizeof actual_digest)) return -17;

        uint8_t le32[4];
        le32[0] = (uint8_t)name_bytes;
        le32[1] = (uint8_t)(name_bytes >> 8);
        le32[2] = (uint8_t)(name_bytes >> 16);
        le32[3] = (uint8_t)(name_bytes >> 24);
        sha256_update(&canonical, le32, sizeof le32);
        sha256_update(&canonical, name, name_bytes);
        le32[0] = (uint8_t)entry.data_bytes;
        le32[1] = (uint8_t)(entry.data_bytes >> 8);
        le32[2] = (uint8_t)(entry.data_bytes >> 16);
        le32[3] = (uint8_t)(entry.data_bytes >> 24);
        sha256_update(&canonical, le32, sizeof le32);
        sha256_update(&canonical, actual_digest, sizeof actual_digest);

        if (entry_name_equal(&entry, "CODE/applied-bus.bin")) {
            view->applied_bus = entry.data;
            view->applied_bus_bytes = entry.data_bytes;
        } else if (entry_name_equal(&entry, "BAG/schema.bin")) {
            view->bag_schema = entry.data;
            view->bag_schema_bytes = entry.data_bytes;
        }
        zip_offset = entry.next_offset;
    }

    if (!require_literal(&p, end, "]")) return -18;
    uint8_t actual_content[32];
    sha256_final(&canonical, actual_content);
    if (!bytes_equal(actual_content, expected_content, sizeof actual_content)) return -19;

    if (zip_offset + 4u > book_bytes) return -20;
    uint32_t trailing = read_le32(book + zip_offset);
    if (trailing != JX64_CENTRAL_SIGNATURE && trailing != JX64_EOCD_SIGNATURE) return -21;
    return 0;
}

static int load_jx64(const uint8_t *book, size_t book_bytes, jx64_book_view *view) {
    static const uint8_t magic[8] = {'J','X','6','4','B','0','0','1'};
    static const uint8_t bag_prefix[] = "jx.bag.container/1\0record\0";
    if (!book || !view || book_bytes < JX64_LOCAL_HEADER_BYTES || book_bytes > JX64_MAX_BOOK_BYTES) return -1;
    zero_bytes(view, sizeof *view);

    jx64_entry header_entry;
    if (parse_local_entry(book, book_bytes, 0u, &header_entry) != 0) return -2;
    if (!entry_name_equal(&header_entry, "JX64/header.bin") || header_entry.data_bytes != JX64_IDENTITY_BYTES) return -3;
    const uint8_t *header = header_entry.data;
    if (!bytes_equal(header, magic, sizeof magic)) return -4;
    uint16_t major = read_le16(header + 8u);
    uint16_t minor = read_le16(header + 10u);
    uint32_t sections = read_le32(header + 12u);
    if (major != 1u || minor != 0u || sections == 0u || sections > 65535u) return -5;

    jx64_entry manifest_entry;
    if (parse_local_entry(book, book_bytes, header_entry.next_offset, &manifest_entry) != 0) return -6;
    if (!entry_name_equal(&manifest_entry, "JX64/manifest.json")) return -7;
    uint8_t manifest_digest[32];
    sha256_bytes(manifest_entry.data, manifest_entry.data_bytes, manifest_digest);
    if (!bytes_equal(manifest_digest, header + 16u, sizeof manifest_digest)) return -8;
    if (!find_literal(manifest_entry.data, manifest_entry.data_bytes, "\"format\":\"jx.64B/1\"")) return -9;
    if (!find_literal(manifest_entry.data, manifest_entry.data_bytes, "\"kind\":\"compiled-book\"")) return -10;

    view->major = major;
    view->minor = minor;
    view->sections = sections;
    int rc = parse_manifest_sections(manifest_entry.data,
                                     manifest_entry.data_bytes,
                                     book,
                                     book_bytes,
                                     manifest_entry.next_offset,
                                     sections,
                                     view);
    if (rc != 0) return -100 + rc;
    if (!view->applied_bus || view->applied_bus_bytes != OSAURA_JX_RUNTIME_PAGE_BYTES) return -11;
    if (!view->bag_schema || view->bag_schema_bytes < sizeof bag_prefix - 1u ||
        !bytes_equal(view->bag_schema, bag_prefix, sizeof bag_prefix - 1u)) return -12;
    return 0;
}

static void bag_init(void) {
    zero_bytes(&g_bag, sizeof g_bag);
}

static void bag_add(uint32_t slot, uint64_t value) {
    if (slot >= JX_BAG_SLOTS) return;
    g_bag.hot[slot] += value;
    g_bag.dirty = 1u;
}

static void bag_set(uint32_t slot, uint64_t value) {
    if (slot >= JX_BAG_SLOTS) return;
    g_bag.hot[slot] = value;
    g_bag.dirty = 1u;
}

static void bag_checkpoint(void) {
    if (!g_bag.dirty) return;
    for (uint32_t i = 0; i < JX_BAG_SLOTS; ++i) g_bag.canonical[i] = g_bag.hot[i];
    ++g_bag.revision;
    ++g_bag.checkpoints;
    g_bag.dirty = 0u;
}

static jx_channel_endpoint *find_endpoint(jx_channel_bus *bus, uint32_t endpoint_id) {
    if (!bus) return NULL;
    for (size_t i = 0; i < JX_CHANNEL_BUS_MAX_ENDPOINTS; ++i)
        if (bus->endpoints[i].in_use && bus->endpoints[i].endpoint_id == endpoint_id)
            return &bus->endpoints[i];
    return NULL;
}

static int endpoint_has_channel(const jx_channel_endpoint *ep, uint16_t channel_id, uint8_t dir_mask) {
    if (!ep) return 0;
    for (size_t i = 0; i < ep->binding_count; ++i) {
        const jx_channel_binding *binding = &ep->bindings[i];
        if (binding->in_use && binding->channel_id == channel_id && (binding->direction & dir_mask)) return 1;
    }
    return 0;
}

static void channel_bus_init(jx_channel_bus *bus, uint32_t active_program_endpoint) {
    zero_bytes(bus, sizeof *bus);
    bus->version = JX_CHANNEL_BUS_VERSION;
    bus->active_program_endpoint = active_program_endpoint;
}

static int channel_bus_add_endpoint(jx_channel_bus *bus,
                                    uint32_t endpoint_id,
                                    jx_channel_receive_fn receive,
                                    void *context) {
    if (!bus || !endpoint_id) return -1;
    if (find_endpoint(bus, endpoint_id)) return -2;
    for (size_t i = 0; i < JX_CHANNEL_BUS_MAX_ENDPOINTS; ++i) {
        if (!bus->endpoints[i].in_use) {
            jx_channel_endpoint *ep = &bus->endpoints[i];
            zero_bytes(ep, sizeof *ep);
            ep->endpoint_id = endpoint_id;
            ep->receive = receive;
            ep->context = context;
            ep->in_use = 1u;
            ++bus->endpoint_count;
            return 0;
        }
    }
    return -3;
}

static int channel_bus_bind(jx_channel_bus *bus,
                            uint32_t endpoint_id,
                            uint16_t channel_id,
                            uint8_t direction) {
    jx_channel_endpoint *ep = find_endpoint(bus, endpoint_id);
    if (!ep || !channel_id || direction < JX_CHANNEL_DIR_IN || direction > JX_CHANNEL_DIR_INOUT) return -1;
    if (ep->binding_count >= JX_CHANNEL_BUS_MAX_CHANNELS) return -2;
    jx_channel_binding *binding = &ep->bindings[ep->binding_count++];
    binding->channel_id = channel_id;
    binding->direction = direction;
    binding->in_use = 1u;
    return 0;
}

static int channel_bus_deliver(jx_channel_bus *bus, const jx_channel_message *message) {
    int deliveries = 0;
    for (size_t i = 0; i < JX_CHANNEL_BUS_MAX_ENDPOINTS; ++i) {
        jx_channel_endpoint *ep = &bus->endpoints[i];
        if (!ep->in_use || ep->endpoint_id == message->source_endpoint) continue;
        if (!endpoint_has_channel(ep, message->channel_id, JX_CHANNEL_DIR_IN)) continue;
        if ((ep->endpoint_id & 0x80000000u) && ep->endpoint_id != bus->active_program_endpoint) continue;
        if (ep->receive) {
            int rc = ep->receive(message->channel_id, message->message_type, message->payload, ep->context);
            if (rc < 0) return rc;
        }
        ++deliveries;
    }
    return deliveries;
}

static int channel_bus_publish(jx_channel_bus *bus,
                               uint32_t source_endpoint,
                               uint16_t channel_id,
                               uint32_t message_type,
                               void *payload) {
    if (!bus || !channel_id) return -1;
    jx_channel_endpoint *source = find_endpoint(bus, source_endpoint);
    if (source && !endpoint_has_channel(source, channel_id, JX_CHANNEL_DIR_OUT)) return -2;
    jx_channel_message message = {channel_id, message_type, payload, source_endpoint};
    if (!bus->paused) return channel_bus_deliver(bus, &message);
    if (bus->queue_count >= JX_CHANNEL_BUS_QUEUE_DEPTH) return -3;
    size_t at = (bus->queue_head + bus->queue_count) % JX_CHANNEL_BUS_QUEUE_DEPTH;
    bus->queue[at] = message;
    ++bus->queue_count;
    return 0;
}

static void channel_bus_pause(jx_channel_bus *bus) {
    if (bus) bus->paused = 1u;
}

static int channel_bus_resume(jx_channel_bus *bus) {
    if (!bus) return -1;
    bus->paused = 0u;
    int delivered = 0;
    while (bus->queue_count) {
        jx_channel_message message = bus->queue[bus->queue_head];
        bus->queue_head = (bus->queue_head + 1u) % JX_CHANNEL_BUS_QUEUE_DEPTH;
        --bus->queue_count;
        int rc = channel_bus_deliver(bus, &message);
        if (rc < 0) return rc;
        delivered += rc;
    }
    return delivered;
}

static int channel_bus_switch_program(jx_channel_bus *bus,
                                      uint32_t expected_old_endpoint,
                                      uint32_t new_endpoint) {
    if (!bus || !bus->paused) return -1;
    if (bus->active_program_endpoint != expected_old_endpoint) return -2;
    if (!find_endpoint(bus, new_endpoint)) return -3;
    bus->active_program_endpoint = new_endpoint;
    return 0;
}

static int program_receive(uint16_t channel_id,
                           uint32_t message_type,
                           void *payload,
                           void *context) {
    (void)payload;
    if (channel_id != JX_RUNTIME_CHANNEL || !context) return -1;
    jx_program_probe *probe = (jx_program_probe *)context;
    ++probe->deliveries;
    bag_add(JX_BAG_CHANNEL_DELIVERIES, 1u);
    bag_set(JX_BAG_LAST_MESSAGE_TYPE, message_type);
    return 0;
}

static int runtime_publish(uint32_t message_type) {
    int rc = channel_bus_publish(&g_bus,
                                 JX_RUNTIME_ENDPOINT,
                                 JX_RUNTIME_CHANNEL,
                                 message_type,
                                 NULL);
    if (rc >= 0) bag_add(JX_BAG_CHANNEL_MESSAGES, 1u);
    return rc;
}

static int runtime_bus_init(void) {
    channel_bus_init(&g_bus, JX_PROGRAM_A);
    g_program_a.endpoint_id = JX_PROGRAM_A;
    g_program_a.deliveries = 0;
    g_program_b.endpoint_id = JX_PROGRAM_B;
    g_program_b.deliveries = 0;

    if (channel_bus_add_endpoint(&g_bus, JX_RUNTIME_ENDPOINT, NULL, NULL) != 0) return 0;
    if (channel_bus_add_endpoint(&g_bus, JX_PROGRAM_A, program_receive, &g_program_a) != 0) return 0;
    if (channel_bus_add_endpoint(&g_bus, JX_PROGRAM_B, program_receive, &g_program_b) != 0) return 0;
    if (channel_bus_bind(&g_bus, JX_RUNTIME_ENDPOINT, JX_RUNTIME_CHANNEL, JX_CHANNEL_DIR_OUT) != 0) return 0;
    if (channel_bus_bind(&g_bus, JX_PROGRAM_A, JX_RUNTIME_CHANNEL, JX_CHANNEL_DIR_INOUT) != 0) return 0;
    if (channel_bus_bind(&g_bus, JX_PROGRAM_B, JX_RUNTIME_CHANNEL, JX_CHANNEL_DIR_INOUT) != 0) return 0;

    /* Exercise the canonical pause/queue/program-switch rule before going live. */
    channel_bus_pause(&g_bus);
    if (runtime_publish(JX_MESSAGE_BOOT) != 0) return 0;
    if (channel_bus_switch_program(&g_bus, JX_PROGRAM_A, JX_PROGRAM_B) != 0) return 0;
    bag_add(JX_BAG_CHANNEL_SWITCHES, 1u);
    if (channel_bus_resume(&g_bus) != 1) return 0;
    if (g_program_a.deliveries != 0u || g_program_b.deliveries != 1u) return 0;

    channel_bus_pause(&g_bus);
    if (channel_bus_switch_program(&g_bus, JX_PROGRAM_B, JX_PROGRAM_A) != 0) return 0;
    bag_add(JX_BAG_CHANNEL_SWITCHES, 1u);
    if (channel_bus_resume(&g_bus) != 0) return 0;
    return g_bus.active_program_endpoint == JX_PROGRAM_A;
}

static void runtime_error(void) {
    ++g_errors;
}

static int execute_applied_entry(uint32_t offset) {
    if (!g_book_loaded || !g_book.applied_bus ||
        offset + JX_SYSTEM_BYTES > g_book.applied_bus_bytes) {
        runtime_error();
        return 0;
    }
    const uint8_t *op = g_book.applied_bus + offset;
    if (op[0] != JX_SYSTEM_ESCAPE || op[1] != JX_SYSTEM_BUS) {
        runtime_error();
        return 0;
    }

    if (op[2] == JX_BUS_TICK) {
        bag_add(JX_BAG_BUS_TICKS, 1u);
        bag_add(JX_BAG_HEARTBEAT, 1u);
        if (runtime_publish(JX_MESSAGE_TICK) < 0) {
            runtime_error();
            return 0;
        }
        return 1;
    }
    if (op[2] == JX_BUS_COLLECT) {
        bag_add(JX_BAG_BUS_COLLECTS, 1u);
        bag_add(JX_BAG_HEARTBEAT, 1u);
        bag_checkpoint();
        return 1;
    }

    runtime_error();
    return 0;
}

static void announce_when_admitted(void) {
    if (g_announced || g_errors || !g_book_loaded || !g_bus_ready ||
        !g_bag.hot[JX_BAG_BUS_TICKS] || !g_bag.hot[JX_BAG_BUS_COLLECTS] ||
        !g_bag.hot[JX_BAG_CHANNEL_DELIVERIES] || !g_bag.checkpoints) return;

    g_announced = 1u;
    serial_text("\nJX 64B: VERIFIED\n");
    serial_text("JX 64B CODE/APPLIED-BUS: LOADED\n");
    serial_text("JX BAG: ACTIVE\n");
    serial_text("JX BAG CHECKPOINT: ACTIVE\n");
    serial_text("JX CHANNEL BUS: ACTIVE\n");
    serial_text("JX CHANNEL SWITCH: ACTIVE\n");
    serial_text("JX RUNTIME: ACTIVE\n");
    serial_text("JX APPLIED ABI: 1\n");
    serial_text("JX PAGE: 7F0001 7F0002\n");
    serial_text("JX BUS.TICK: ACTIVE\n");
    serial_text("JX BUS.COLLECT: ACTIVE\n");
}

int osaura_jx_runtime_load_book(const void *bytes, uint64_t size) {
    if (g_active || g_book_loaded || !bytes || !size || size > JX64_MAX_BOOK_BYTES ||
        size > (uint64_t)(~(size_t)0)) return -1;
    jx64_book_view view;
    int rc = load_jx64((const uint8_t *)bytes, (size_t)size, &view);
    if (rc != 0) {
        ++g_errors;
        return rc;
    }
    g_book = view;
    g_book_loaded = 1u;
    return 0;
}

int osaura_jx_runtime_book_loaded(void) {
    return g_book_loaded != 0u;
}

__attribute__((noreturn)) void osaura_jx_runtime_task(void) {
    if (!g_book_loaded) {
        runtime_error();
        serial_text("\nJX RUNTIME: BOOK MISSING\n");
        for (;;) __asm__ volatile("hlt");
    }

    bag_init();
    if (!runtime_bus_init()) {
        runtime_error();
        serial_text("\nJX RUNTIME: CHANNEL BUS FAILED\n");
        for (;;) __asm__ volatile("hlt");
    }
    g_bus_ready = 1u;
    g_active = 1u;

    for (;;) {
        /* AppliedBytecode::runtimeBusPage() exposes entrypoints, not a stream. */
        (void)execute_applied_entry(OSAURA_JX_RUNTIME_TICK_OFFSET);
        (void)execute_applied_entry(OSAURA_JX_RUNTIME_COLLECT_OFFSET);
        announce_when_admitted();
        __asm__ volatile("hlt");
    }
}

int osaura_jx_runtime_active(void) {
    return g_active != 0u;
}

uint64_t osaura_jx_runtime_heartbeat(void) {
    return g_bag.hot[JX_BAG_HEARTBEAT];
}

uint64_t osaura_jx_runtime_bus_ticks(void) {
    return g_bag.hot[JX_BAG_BUS_TICKS];
}

uint64_t osaura_jx_runtime_bus_collects(void) {
    return g_bag.hot[JX_BAG_BUS_COLLECTS];
}

uint64_t osaura_jx_runtime_bag_revision(void) {
    return g_bag.revision;
}

uint64_t osaura_jx_runtime_bag_checkpoints(void) {
    return g_bag.checkpoints;
}

uint64_t osaura_jx_runtime_channel_messages(void) {
    return g_bag.hot[JX_BAG_CHANNEL_MESSAGES];
}

uint64_t osaura_jx_runtime_channel_deliveries(void) {
    return g_bag.hot[JX_BAG_CHANNEL_DELIVERIES];
}

uint64_t osaura_jx_runtime_channel_switches(void) {
    return g_bag.hot[JX_BAG_CHANNEL_SWITCHES];
}

uint64_t osaura_jx_runtime_errors(void) {
    return g_errors;
}
