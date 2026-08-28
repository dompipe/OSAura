#include "net.h"
#include "e1000.h"

#include <stddef.h>
#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0x0cf8u
#define PCI_CONFIG_DATA 0x0cfcu
#define PCI_CLASS_NETWORK 0x02u
#define OSAURA_NET_MAX_DEVICES 16u
#define NET_FRAME_MAX 1600u
#define ETH_TYPE_IPV4 0x0800u
#define ETH_TYPE_ARP 0x0806u
#define ARP_HTYPE_ETHERNET 1u
#define ARP_OP_REQUEST 1u
#define ARP_OP_REPLY 2u
#define IP_PROTOCOL_ICMP 1u
#define ICMP_ECHO_REPLY 0u
#define ICMP_ECHO_REQUEST 8u
#define NET_TIMEOUT_TICKS 150u

extern volatile uint64_t osaura_ticks;

typedef struct __attribute__((packed)) {
    uint8_t destination[6];
    uint8_t source[6];
    uint16_t type;
} ethernet_header;

typedef struct __attribute__((packed)) {
    uint16_t hardware_type;
    uint16_t protocol_type;
    uint8_t hardware_bytes;
    uint8_t protocol_bytes;
    uint16_t operation;
    uint8_t sender_mac[6];
    uint8_t sender_ip[4];
    uint8_t target_mac[6];
    uint8_t target_ip[4];
} arp_packet;

typedef struct __attribute__((packed)) {
    uint8_t version_ihl;
    uint8_t dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint8_t source[4];
    uint8_t destination[4];
} ipv4_header;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
    uint8_t payload[16];
} icmp_echo;

typedef struct {
    uint8_t address[4];
    uint8_t mask[4];
    uint8_t gateway[4];
    uint8_t configured;
} ipv4_config;

typedef struct {
    uint8_t ip[4];
    uint8_t mac[6];
    uint8_t valid;
} arp_cache_entry;

static osaura_net_device g_devices[OSAURA_NET_MAX_DEVICES];
static uint32_t g_device_count;
static ipv4_config g_ipv4;
static arp_cache_entry g_arp;
static uint8_t g_driver_ready;
static uint16_t g_ip_identification;
static uint16_t g_ping_sequence;
static uint8_t g_frame[NET_FRAME_MAX];

static inline uint32_t in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void out32(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline void cpu_pause(void) { __asm__ volatile("pause"); }

static uint16_t host_to_be16(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
}

static uint16_t be16_to_host(uint16_t value) {
    return host_to_be16(value);
}

static uint32_t pci_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return 0x80000000u |
           ((uint32_t)bus << 16) |
           ((uint32_t)device << 11) |
           ((uint32_t)function << 8) |
           (offset & 0xfcu);
}

static uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    out32(PCI_CONFIG_ADDRESS, pci_address(bus, device, function, offset));
    return in32(PCI_CONFIG_DATA);
}

static void zero_bytes(void *ptr, size_t bytes) {
    uint8_t *p = (uint8_t *)ptr;
    while (bytes--) *p++ = 0u;
}

static void copy_bytes(void *target, const void *source, size_t bytes) {
    uint8_t *out = (uint8_t *)target;
    const uint8_t *in = (const uint8_t *)source;
    while (bytes--) *out++ = *in++;
}

static int bytes_equal(const uint8_t *a, const uint8_t *b, size_t bytes) {
    while (bytes--) if (*a++ != *b++) return 0;
    return 1;
}

static int ascii_upper(int c) {
    return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}

static int starts_command(const char *line, const char *command, const char **args) {
    const char *a = line;
    const char *b = command;
    while (*b && *a && ascii_upper((unsigned char)*a) == ascii_upper((unsigned char)*b)) {
        ++a;
        ++b;
    }
    if (*b) return 0;
    if (*a && *a != ' ') return 0;
    while (*a == ' ') ++a;
    if (args) *args = a;
    return 1;
}

static const char *next_token(const char *text, char *out, size_t capacity) {
    size_t n = 0u;
    while (*text == ' ') ++text;
    while (*text && *text != ' ') {
        if (n + 1u < capacity) out[n++] = *text;
        ++text;
    }
    out[n] = 0;
    while (*text == ' ') ++text;
    return text;
}

static int parse_ipv4(const char *text, uint8_t out[4]) {
    if (!text || !*text) return 0;
    for (uint32_t part = 0; part < 4u; ++part) {
        uint32_t value = 0u;
        uint32_t digits = 0u;
        while (*text >= '0' && *text <= '9') {
            value = value * 10u + (uint32_t)(*text - '0');
            if (value > 255u) return 0;
            ++text;
            ++digits;
        }
        if (!digits) return 0;
        out[part] = (uint8_t)value;
        if (part < 3u) {
            if (*text++ != '.') return 0;
        } else if (*text) {
            return 0;
        }
    }
    return 1;
}

static void write_u32(osaura_net_write_fn write, uint32_t value) {
    char digits[11];
    uint32_t count = 0u;
    if (!write) return;
    if (!value) { write("0"); return; }
    while (value && count < sizeof digits) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count) {
        char out[2] = {digits[--count], 0};
        write(out);
    }
}

static void write_hex16(osaura_net_write_fn write, uint16_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char out[7] = {'0','x','0','0','0','0',0};
    out[2] = hex[(value >> 12) & 0x0fu];
    out[3] = hex[(value >> 8) & 0x0fu];
    out[4] = hex[(value >> 4) & 0x0fu];
    out[5] = hex[value & 0x0fu];
    write(out);
}

static void write_ipv4(osaura_net_write_fn write, const uint8_t ip[4]) {
    for (uint32_t i = 0; i < 4u; ++i) {
        if (i) write(".");
        write_u32(write, ip[i]);
    }
}

static void write_mac(osaura_net_write_fn write, const uint8_t mac[6]) {
    static const char hex[] = "0123456789ABCDEF";
    for (uint32_t i = 0; i < 6u; ++i) {
        char part[3] = {hex[mac[i] >> 4], hex[mac[i] & 0x0fu], 0};
        if (i) write(":");
        write(part);
    }
}

static uint16_t checksum16(const void *data, size_t bytes) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0u;
    while (bytes > 1u) {
        sum += ((uint16_t)p[0] << 8) | p[1];
        p += 2;
        bytes -= 2u;
    }
    if (bytes) sum += (uint16_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xffffu) + (sum >> 16);
    return (uint16_t)~sum;
}

static int same_subnet(const uint8_t a[4], const uint8_t b[4]) {
    for (uint32_t i = 0; i < 4u; ++i)
        if ((a[i] & g_ipv4.mask[i]) != (b[i] & g_ipv4.mask[i])) return 0;
    return 1;
}

static void route_next_hop(const uint8_t target[4], uint8_t next[4]) {
    if (same_subnet(g_ipv4.address, target)) copy_bytes(next, target, 4u);
    else copy_bytes(next, g_ipv4.gateway, 4u);
}

static int ethernet_send(const uint8_t destination[6], uint16_t type,
                         const void *payload, uint16_t payload_bytes) {
    if (!g_driver_ready || payload_bytes > NET_FRAME_MAX - sizeof(ethernet_header)) return 0;
    ethernet_header *eth = (ethernet_header *)g_frame;
    const osaura_mac_address *mac = osaura_e1000_mac();
    if (!mac) return 0;
    copy_bytes(eth->destination, destination, 6u);
    copy_bytes(eth->source, mac->bytes, 6u);
    eth->type = host_to_be16(type);
    copy_bytes(g_frame + sizeof *eth, payload, payload_bytes);
    return osaura_e1000_transmit(g_frame, (uint16_t)(sizeof *eth + payload_bytes));
}

static void arp_learn(const uint8_t ip[4], const uint8_t mac[6]) {
    copy_bytes(g_arp.ip, ip, 4u);
    copy_bytes(g_arp.mac, mac, 6u);
    g_arp.valid = 1u;
}

static void arp_reply(const arp_packet *request) {
    arp_packet reply;
    const osaura_mac_address *mac = osaura_e1000_mac();
    if (!mac) return;
    reply.hardware_type = host_to_be16(ARP_HTYPE_ETHERNET);
    reply.protocol_type = host_to_be16(ETH_TYPE_IPV4);
    reply.hardware_bytes = 6u;
    reply.protocol_bytes = 4u;
    reply.operation = host_to_be16(ARP_OP_REPLY);
    copy_bytes(reply.sender_mac, mac->bytes, 6u);
    copy_bytes(reply.sender_ip, g_ipv4.address, 4u);
    copy_bytes(reply.target_mac, request->sender_mac, 6u);
    copy_bytes(reply.target_ip, request->sender_ip, 4u);
    (void)ethernet_send(request->sender_mac, ETH_TYPE_ARP, &reply, sizeof reply);
}

static void handle_arp(const uint8_t *payload, uint16_t bytes) {
    if (bytes < sizeof(arp_packet)) return;
    const arp_packet *arp = (const arp_packet *)payload;
    if (be16_to_host(arp->hardware_type) != ARP_HTYPE_ETHERNET ||
        be16_to_host(arp->protocol_type) != ETH_TYPE_IPV4 ||
        arp->hardware_bytes != 6u || arp->protocol_bytes != 4u) return;
    arp_learn(arp->sender_ip, arp->sender_mac);
    if (g_ipv4.configured && be16_to_host(arp->operation) == ARP_OP_REQUEST &&
        bytes_equal(arp->target_ip, g_ipv4.address, 4u)) arp_reply(arp);
}

static int handle_frame(uint8_t wanted_icmp_sequence, const uint8_t target_ip[4]) {
    uint16_t bytes = 0u;
    if (!osaura_e1000_receive(g_frame, sizeof g_frame, &bytes)) return 0;
    if (bytes < sizeof(ethernet_header)) return 0;
    const ethernet_header *eth = (const ethernet_header *)g_frame;
    uint16_t type = be16_to_host(eth->type);
    const uint8_t *payload = g_frame + sizeof *eth;
    uint16_t payload_bytes = (uint16_t)(bytes - sizeof *eth);
    if (type == ETH_TYPE_ARP) {
        handle_arp(payload, payload_bytes);
        return 0;
    }
    if (type != ETH_TYPE_IPV4 || payload_bytes < sizeof(ipv4_header)) return 0;

    const ipv4_header *ip = (const ipv4_header *)payload;
    uint8_t ihl = (uint8_t)((ip->version_ihl & 0x0fu) * 4u);
    if ((ip->version_ihl >> 4) != 4u || ihl < 20u || payload_bytes < ihl) return 0;
    if (ip->protocol != IP_PROTOCOL_ICMP || !bytes_equal(ip->destination, g_ipv4.address, 4u)) return 0;
    if (!bytes_equal(ip->source, target_ip, 4u)) return 0;
    uint16_t total = be16_to_host(ip->total_length);
    if (total < ihl + sizeof(icmp_echo) || total > payload_bytes) return 0;
    const icmp_echo *icmp = (const icmp_echo *)(payload + ihl);
    return icmp->type == ICMP_ECHO_REPLY && icmp->code == 0u &&
           be16_to_host(icmp->identifier) == 0x4f53u &&
           be16_to_host(icmp->sequence) == wanted_icmp_sequence;
}

static int arp_resolve(const uint8_t ip[4], uint8_t mac_out[6]) {
    if (g_arp.valid && bytes_equal(g_arp.ip, ip, 4u)) {
        copy_bytes(mac_out, g_arp.mac, 6u);
        return 1;
    }

    arp_packet request;
    const osaura_mac_address *mac = osaura_e1000_mac();
    static const uint8_t broadcast[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
    if (!mac) return 0;
    request.hardware_type = host_to_be16(ARP_HTYPE_ETHERNET);
    request.protocol_type = host_to_be16(ETH_TYPE_IPV4);
    request.hardware_bytes = 6u;
    request.protocol_bytes = 4u;
    request.operation = host_to_be16(ARP_OP_REQUEST);
    copy_bytes(request.sender_mac, mac->bytes, 6u);
    copy_bytes(request.sender_ip, g_ipv4.address, 4u);
    zero_bytes(request.target_mac, 6u);
    copy_bytes(request.target_ip, ip, 4u);
    if (!ethernet_send(broadcast, ETH_TYPE_ARP, &request, sizeof request)) return 0;

    uint64_t deadline = osaura_ticks + NET_TIMEOUT_TICKS;
    while (osaura_ticks < deadline) {
        uint16_t bytes = 0u;
        if (osaura_e1000_receive(g_frame, sizeof g_frame, &bytes) && bytes >= sizeof(ethernet_header)) {
            const ethernet_header *eth = (const ethernet_header *)g_frame;
            if (be16_to_host(eth->type) == ETH_TYPE_ARP)
                handle_arp(g_frame + sizeof *eth, (uint16_t)(bytes - sizeof *eth));
            if (g_arp.valid && bytes_equal(g_arp.ip, ip, 4u)) {
                copy_bytes(mac_out, g_arp.mac, 6u);
                return 1;
            }
        }
        cpu_pause();
    }
    return 0;
}

static int ping_ipv4(const uint8_t target[4]) {
    uint8_t next_hop[4];
    uint8_t destination_mac[6];
    route_next_hop(target, next_hop);
    if (!arp_resolve(next_hop, destination_mac)) return -2;

    uint8_t packet[sizeof(ipv4_header) + sizeof(icmp_echo)];
    ipv4_header *ip = (ipv4_header *)packet;
    icmp_echo *icmp = (icmp_echo *)(packet + sizeof *ip);
    zero_bytes(packet, sizeof packet);

    ip->version_ihl = 0x45u;
    ip->total_length = host_to_be16(sizeof packet);
    ip->identification = host_to_be16(++g_ip_identification);
    ip->flags_fragment = host_to_be16(0x4000u);
    ip->ttl = 64u;
    ip->protocol = IP_PROTOCOL_ICMP;
    copy_bytes(ip->source, g_ipv4.address, 4u);
    copy_bytes(ip->destination, target, 4u);
    ip->checksum = host_to_be16(checksum16(ip, sizeof *ip));

    uint16_t sequence = ++g_ping_sequence;
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->identifier = host_to_be16(0x4f53u);
    icmp->sequence = host_to_be16(sequence);
    for (uint32_t i = 0; i < sizeof icmp->payload; ++i) icmp->payload[i] = (uint8_t)(0x41u + i);
    icmp->checksum = host_to_be16(checksum16(icmp, sizeof *icmp));

    if (!ethernet_send(destination_mac, ETH_TYPE_IPV4, packet, sizeof packet)) return -3;
    uint64_t deadline = osaura_ticks + NET_TIMEOUT_TICKS;
    while (osaura_ticks < deadline) {
        if (handle_frame((uint8_t)sequence, target)) return 1;
        cpu_pause();
    }
    return 0;
}

static int e1000_device_id(uint16_t id) {
    switch (id) {
        case 0x100eu: case 0x100fu: case 0x1010u: case 0x107cu:
        case 0x10d3u: case 0x10e5u: return 1;
        default: return 0;
    }
}

static void print_nics(osaura_net_write_fn write) {
    write("NETWORK DEVICES: ");
    write_u32(write, g_device_count);
    write("\n");
    if (!g_device_count) { write("NO PCI NETWORK CONTROLLER FOUND\n"); return; }
    for (uint32_t i = 0; i < g_device_count; ++i) {
        const osaura_net_device *dev = &g_devices[i];
        write("NIC "); write_u32(write, i);
        write(" PCI "); write_u32(write, dev->bus); write(":");
        write_u32(write, dev->device); write("."); write_u32(write, dev->function);
        write(" VENDOR "); write_hex16(write, dev->vendor_id);
        write(" DEVICE "); write_hex16(write, dev->device_id);
        if (dev->vendor_id == 0x8086u && e1000_device_id(dev->device_id)) write(" E1000");
        write("\n");
    }
}

static void print_ip(osaura_net_write_fn write) {
    write(g_driver_ready ? "ETHERNET E1000: ACTIVE\n" : "ETHERNET DRIVER: NOT ACTIVE\n");
    if (!g_ipv4.configured) { write("IPV4: NOT CONFIGURED\n"); return; }
    write("IPV4 ADDRESS: "); write_ipv4(write, g_ipv4.address);
    write(" MASK: "); write_ipv4(write, g_ipv4.mask);
    write(" GATEWAY: "); write_ipv4(write, g_ipv4.gateway); write("\n");
    const osaura_mac_address *mac = osaura_e1000_mac();
    if (mac) { write("MAC: "); write_mac(write, mac->bytes); write("\n"); }
}

static void print_capabilities(osaura_net_write_fn write) {
    write("CORE NETWORK UTILITIES: NET IP ROUTE ARP PING DNS RESOLVE NETSTAT CURL WGET FETCH\n");
    write(g_device_count ? "NIC DISCOVERY: ACTIVE\n" : "NIC DISCOVERY: NO DEVICE\n");
    write(g_driver_ready ? "ETHERNET E1000: ACTIVE\n" : "ETHERNET E1000: NO SUPPORTED DEVICE\n");
    write("ARP IPV4 ICMP: ACTIVE\n");
    write("IPV6 UDP TCP DHCP DNS TLS: PENDING\n");
    write("HTTP HTTPS DOWNLOAD: PENDING\n");
}

static void require_argument(osaura_net_write_fn write, const char *usage) {
    write("USAGE: "); write(usage); write("\n");
}

static void transport_pending(osaura_net_write_fn write, const char *operation, const char *target) {
    write(operation); write(": CORE COMMAND PRESENT; REQUIRED TRANSPORT NOT ONLINE");
    if (target && *target) { write(" TARGET "); write(target); }
    write("\n");
}

void osaura_net_init(void) {
    g_device_count = 0u;
    g_driver_ready = 0u;
    zero_bytes(&g_ipv4, sizeof g_ipv4);
    zero_bytes(&g_arp, sizeof g_arp);
    for (uint16_t bus = 0; bus < 256u && g_device_count < OSAURA_NET_MAX_DEVICES; ++bus) {
        for (uint8_t device = 0; device < 32u && g_device_count < OSAURA_NET_MAX_DEVICES; ++device) {
            uint32_t id0 = pci_read32((uint8_t)bus, device, 0u, 0u);
            if ((id0 & 0xffffu) == 0xffffu) continue;
            uint8_t header = (uint8_t)(pci_read32((uint8_t)bus, device, 0u, 0x0cu) >> 16);
            uint8_t functions = (header & 0x80u) ? 8u : 1u;
            for (uint8_t function = 0; function < functions && g_device_count < OSAURA_NET_MAX_DEVICES; ++function) {
                uint32_t id = pci_read32((uint8_t)bus, device, function, 0u);
                if ((id & 0xffffu) == 0xffffu) continue;
                uint32_t class_reg = pci_read32((uint8_t)bus, device, function, 0x08u);
                if ((uint8_t)(class_reg >> 24) != PCI_CLASS_NETWORK) continue;
                osaura_net_device *out = &g_devices[g_device_count++];
                out->vendor_id = (uint16_t)id;
                out->device_id = (uint16_t)(id >> 16);
                out->bus = (uint8_t)bus;
                out->device = device;
                out->function = function;
                out->class_code = (uint8_t)(class_reg >> 24);
                out->subclass = (uint8_t)(class_reg >> 16);
                out->prog_if = (uint8_t)(class_reg >> 8);
                if (!g_driver_ready && out->vendor_id == 0x8086u && e1000_device_id(out->device_id))
                    g_driver_ready = (uint8_t)osaura_e1000_init(out->bus, out->device, out->function);
            }
        }
    }
}

uint32_t osaura_net_device_count(void) { return g_device_count; }

const osaura_net_device *osaura_net_device_at(uint32_t index) {
    return index < g_device_count ? &g_devices[index] : NULL;
}

int osaura_net_command(const char *line, osaura_net_write_fn write) {
    const char *args = NULL;
    if (!line || !write) return 0;

    if (starts_command(line, "NET", &args)) {
        if (!*args || starts_command(args, "STATUS", NULL)) print_capabilities(write);
        else if (starts_command(args, "DEVICES", NULL)) print_nics(write);
        else write("NET: USE STATUS OR DEVICES\n");
        return 1;
    }
    if (starts_command(line, "IP", &args)) {
        const char *set_args = NULL;
        if (starts_command(args, "SET", &set_args)) {
            char a[16], m[16], g[16];
            set_args = next_token(set_args, a, sizeof a);
            set_args = next_token(set_args, m, sizeof m);
            (void)next_token(set_args, g, sizeof g);
            if (!g_driver_ready) write("IP SET: NO SUPPORTED ETHERNET DRIVER\n");
            else if (!parse_ipv4(a, g_ipv4.address) || !parse_ipv4(m, g_ipv4.mask) || !parse_ipv4(g, g_ipv4.gateway))
                require_argument(write, "IP SET <ADDRESS> <MASK> <GATEWAY>");
            else {
                g_ipv4.configured = 1u;
                g_arp.valid = 0u;
                write("IPV4 CONFIGURED\n");
                print_ip(write);
            }
        } else print_ip(write);
        return 1;
    }
    if (starts_command(line, "ROUTE", &args)) {
        (void)args;
        if (!g_ipv4.configured) write("ROUTE TABLE: IPV4 NOT CONFIGURED\n");
        else { write("DEFAULT VIA "); write_ipv4(write, g_ipv4.gateway); write("\n"); }
        return 1;
    }
    if (starts_command(line, "ARP", &args)) {
        (void)args;
        if (!g_arp.valid) write("ARP CACHE: EMPTY\n");
        else { write("ARP "); write_ipv4(write, g_arp.ip); write(" = "); write_mac(write, g_arp.mac); write("\n"); }
        return 1;
    }
    if (starts_command(line, "NETSTAT", &args)) {
        (void)args;
        write("SOCKET TABLE: EMPTY; TCP/UDP PENDING\n");
        return 1;
    }
    if (starts_command(line, "PING", &args)) {
        uint8_t target[4];
        if (!*args) require_argument(write, "PING <IPV4>");
        else if (!g_driver_ready) write("PING: ETHERNET DRIVER NOT ACTIVE\n");
        else if (!g_ipv4.configured) write("PING: CONFIGURE IPV4 WITH IP SET FIRST\n");
        else if (!parse_ipv4(args, target)) write("PING: IPV4 LITERAL REQUIRED UNTIL DNS IS ONLINE\n");
        else {
            write("PING "); write_ipv4(write, target); write(" ... ");
            int rc = ping_ipv4(target);
            if (rc == 1) write("REPLY\n");
            else if (rc == -2) write("ARP TIMEOUT\n");
            else if (rc == -3) write("TX FAILED\n");
            else write("TIMEOUT\n");
        }
        return 1;
    }
    if (starts_command(line, "DNS", &args) || starts_command(line, "RESOLVE", &args)) {
        if (!*args) require_argument(write, "DNS <HOST>");
        else transport_pending(write, "DNS", args);
        return 1;
    }
    if (starts_command(line, "CURL", &args)) {
        if (!*args) require_argument(write, "CURL <URL>");
        else transport_pending(write, "CURL", args);
        return 1;
    }
    if (starts_command(line, "WGET", &args)) {
        if (!*args) require_argument(write, "WGET <URL>");
        else transport_pending(write, "WGET", args);
        return 1;
    }
    if (starts_command(line, "FETCH", &args)) {
        if (!*args) require_argument(write, "FETCH <URL>");
        else transport_pending(write, "FETCH", args);
        return 1;
    }
    return 0;
}
