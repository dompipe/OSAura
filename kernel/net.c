#include "net.h"

#include <stddef.h>
#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0x0cf8u
#define PCI_CONFIG_DATA 0x0cfcu
#define PCI_CLASS_NETWORK 0x02u
#define OSAURA_NET_MAX_DEVICES 16u

static osaura_net_device g_devices[OSAURA_NET_MAX_DEVICES];
static uint32_t g_device_count;

static inline uint32_t in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void out32(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
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

static void write_u32(osaura_net_write_fn write, uint32_t value) {
    char digits[11];
    uint32_t count = 0u;
    if (!write) return;
    if (!value) {
        write("0");
        return;
    }
    while (value && count < sizeof digits) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count) {
        char out[2];
        out[0] = digits[--count];
        out[1] = 0;
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

static void print_nics(osaura_net_write_fn write) {
    if (!write) return;
    write("NETWORK DEVICES: ");
    write_u32(write, g_device_count);
    write("\n");
    if (!g_device_count) {
        write("NO PCI NETWORK CONTROLLER FOUND\n");
        return;
    }
    for (uint32_t i = 0; i < g_device_count; ++i) {
        const osaura_net_device *dev = &g_devices[i];
        write("NIC ");
        write_u32(write, i);
        write(" PCI ");
        write_u32(write, dev->bus);
        write(":");
        write_u32(write, dev->device);
        write(".");
        write_u32(write, dev->function);
        write(" VENDOR ");
        write_hex16(write, dev->vendor_id);
        write(" DEVICE ");
        write_hex16(write, dev->device_id);
        write("\n");
    }
}

static void print_capabilities(osaura_net_write_fn write) {
    write("CORE NETWORK UTILITIES: NET IP ROUTE ARP PING DNS RESOLVE NETSTAT CURL WGET FETCH\n");
    write(g_device_count ? "NIC DISCOVERY: ACTIVE\n" : "NIC DISCOVERY: NO DEVICE\n");
    write("ETHERNET DRIVER: PENDING\n");
    write("ARP IPV4 IPV6 ICMP UDP TCP DHCP DNS TLS: PENDING\n");
    write("HTTP HTTPS DOWNLOAD: PENDING\n");
}

static void require_argument(osaura_net_write_fn write, const char *usage) {
    write("USAGE: ");
    write(usage);
    write("\n");
}

static void transport_pending(osaura_net_write_fn write, const char *operation, const char *target) {
    write(operation);
    write(": CORE COMMAND PRESENT; TRANSPORT STACK NOT ONLINE");
    if (target && *target) {
        write(" TARGET ");
        write(target);
    }
    write("\n");
}

void osaura_net_init(void) {
    g_device_count = 0u;
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
                uint8_t class_code = (uint8_t)(class_reg >> 24);
                if (class_code != PCI_CLASS_NETWORK) continue;
                osaura_net_device *out = &g_devices[g_device_count++];
                out->vendor_id = (uint16_t)(id & 0xffffu);
                out->device_id = (uint16_t)(id >> 16);
                out->bus = (uint8_t)bus;
                out->device = device;
                out->function = function;
                out->class_code = class_code;
                out->subclass = (uint8_t)(class_reg >> 16);
                out->prog_if = (uint8_t)(class_reg >> 8);
            }
        }
    }
}

uint32_t osaura_net_device_count(void) {
    return g_device_count;
}

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
        print_nics(write);
        write("IP ADDRESSING: PENDING ETHERNET DRIVER/DHCP\n");
        return 1;
    }
    if (starts_command(line, "ROUTE", &args)) {
        (void)args;
        write("ROUTE TABLE: EMPTY; IPV4/IPV6 ROUTING PENDING\n");
        return 1;
    }
    if (starts_command(line, "ARP", &args)) {
        (void)args;
        write("ARP CACHE: EMPTY; ETHERNET/ARP PENDING\n");
        return 1;
    }
    if (starts_command(line, "NETSTAT", &args)) {
        (void)args;
        write("SOCKET TABLE: EMPTY; TCP/UDP PENDING\n");
        return 1;
    }
    if (starts_command(line, "PING", &args)) {
        if (!*args) require_argument(write, "PING <HOST>");
        else transport_pending(write, "PING", args);
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
