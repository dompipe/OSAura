#ifdef _WIN32

#if !defined(_WIN64)
#error WSJX requires a native 64-bit Windows target (_WIN64).
#endif

#include "../../include/osaura/socket.h"
#include "../../kernel/security.h"
#include "../../runtime/jx/jx11-display.h"
#include "socket-winsock.h"
#include "display-win32.h"
#include "vfs64.h"
#include "runtime64.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define WSJX_VERSION "0.4-dev"
#define LINE_BYTES 512u
#define WSJX_DISPLAY_WIDTH 1024u
#define WSJX_DISPLAY_HEIGHT 768u

typedef char wsjx_pointer_width_must_be_64_bits[(sizeof(void *) == 8u) ? 1 : -1];
typedef char wsjx_uintptr_width_must_be_64_bits[(sizeof(uintptr_t) == 8u) ? 1 : -1];
typedef char wsjx_size_width_must_be_64_bits[(sizeof(size_t) == 8u) ? 1 : -1];

typedef struct {
    uint8_t jxl_mode;
    uint8_t socket_ready;
    uint8_t display_ready;
    uint8_t vfs_ready;
    uint8_t clock_ready;
    uint8_t memory_ready;
    uint8_t task_ready;
    uint8_t ipc_ready;
    uint8_t input_ready;
    uint32_t loaded_bytes;
    char loaded_path[MAX_PATH];
} wsjx_state;

static int text_equal(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (toupper(ca) != toupper(cb)) return 0;
    }
    return *a == 0 && *b == 0;
}

static int starts_with(const char *text, const char *prefix) {
    while (*prefix) {
        if (!*text) return 0;
        if (toupper((unsigned char)*text++) != toupper((unsigned char)*prefix++)) return 0;
    }
    return 1;
}

static void trim_line(char *line) {
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\r' || line[n - 1] == '\n' || line[n - 1] == ' ' || line[n - 1] == '\t'))
        line[--n] = 0;
}

static int copy_path(char *dst, size_t capacity, const char *src) {
    size_t n;
    if (!dst || !capacity || !src) return -1;
    n = strlen(src);
    if (n >= capacity) return -2;
    memcpy(dst, src, n + 1u);
    return 0;
}

static void print_platform(void) {
    puts("PLATFORM: WSJX64 / AMD64");
    printf("POINTER WIDTH: %u\n", (unsigned)(sizeof(void *) * 8u));
    puts("USER64: WINDOWS USER API (User32.lib x64)");
    puts("GDI64: WINDOWS GDI API (Gdi32.lib x64)");
    puts("WINSOCK64: WINDOWS SOCKET API (Ws2_32.lib x64)");
    puts("VFS64: WINDOWS FILE API BEHIND JX HANDLES");
    puts("CLOCK64: QUERY PERFORMANCE COUNTER");
    puts("MEMORY64: VIRTUALALLOC BEHIND JX ALLOCATION IDS");
    puts("INPUT64: WINDOWS CONSOLE INPUT EVENTS");
    puts("TASK64/JOB64: HOSTED LOGICAL TASK STATE");
    puts("IPC64: IN-PROCESS OSAURA QUEUES");
    puts("KERNEL64: WINDOWS NT x64 HOST MECHANISMS");
}

static void print_banner(const wsjx_state *state) {
    puts("WSJX64 - WINDOWS SUBSYSTEM FOR JX64");
    printf("VERSION: %s\n", WSJX_VERSION);
    print_platform();
    puts("JX64 HOT ABI V4: ACTIVE");
    printf("MODE: %s\n", state->jxl_mode ? "JXL" : "JX");
    puts("SECURITY SUBJECT 1: ACTIVE");
    printf("NETWORK: %s\n", state->socket_ready ? "WINSOCK64 BACKEND" : "UNAVAILABLE");
    printf("JX11 DISPLAY: %s\n", state->display_ready ? "USER64/GDI64 DIB BACKEND" : "UNAVAILABLE");
    printf("VFS64: %s\n", state->vfs_ready ? "ACTIVE" : "UNAVAILABLE");
    puts("F0-FF: RESERVED");
    puts("");
}

static void print_help(void) {
    puts("HELP                 show commands");
    puts("ABOUT                show WSJX64 identity");
    puts("PLATFORM             show 64-bit host mechanisms");
    puts("STATUS               show subsystem state");
    puts("MODE JX              select normal JX session mode");
    puts("MODE JXL             select JXL session mode");
    puts("CAPS                 show JX subject capabilities");
    puts("SOCKET               open/close a JX TCP socket through Winsock64");
    puts("MOUNTS               show the isolated VFS64 root");
    puts("VFS TEST             write as kernel, read as JX through VFS64");
    puts("LOAD /path           inspect .64B/JXL module through VFS64");
    puts("UNLOAD               release loaded module state");
    puts("CLOCK                show CLOCK64 ticks and milliseconds");
    puts("MEM TEST             allocate/map/free through MEMORY64");
    puts("TASKS                show TASK64/JOB64 logical tasks");
    puts("IPC TEST             round-trip one IPC64 message");
    puts("INPUT                show INPUT64 console state");
    puts("DISPLAY              show JX11 display geometry");
    puts("DISPLAY OPEN         show the JX11 Windows surface");
    puts("DISPLAY TEST         draw through JX11 and present");
    puts("CLEAR                clear the console");
    puts("EXIT                 leave WSJX64");
}

static void print_caps(void) {
    uint64_t caps = osaura_security_snapshot(OSAURA_SECURITY_JX_SUBJECT);
    printf("SUBJECT 1 CAPABILITIES: 0x%016llX\n", (unsigned long long)caps);
    printf("NETWORK: %s\n", (caps & OSAURA_CAP_NETWORK) ? "YES" : "NO");
    printf("VFS READ: %s\n", (caps & OSAURA_CAP_VFS_READ) ? "YES" : "NO");
    printf("VFS WRITE: %s\n", (caps & OSAURA_CAP_VFS_WRITE) ? "YES" : "NO");
    printf("BOOK LOAD: %s\n", (caps & OSAURA_CAP_BOOK_LOAD) ? "YES" : "NO");
    printf("ADMIN: %s\n", (caps & OSAURA_CAP_ADMIN) ? "YES" : "NO");
}

static int socket_self_test(void) {
    uint32_t socket_id = OSAURA_SOCKET_NONE;
    int rc = osaura_socket_open_as(OSAURA_SECURITY_JX_SUBJECT,
                                   OSAURA_SOCKET_AF_IPV4,
                                   OSAURA_SOCKET_TYPE_STREAM,
                                   &socket_id);
    if (rc != 0) return rc;
    if (socket_id == OSAURA_SOCKET_NONE) return -100;
    return osaura_socket_close_as(OSAURA_SECURITY_JX_SUBJECT, socket_id);
}

static int display_info(void) {
    osaura_jx11_display_info info;
    int rc = osaura_jx11_display_get_info(&info);
    if (rc != 0) return rc;
    printf("JX11 DISPLAY: %ux%u STRIDE %u FORMAT %u\n",
           info.width, info.height, info.stride_pixels, info.pixel_format);
    return 0;
}

static int display_test(void) {
    osaura_jx11_fill_request panel = {64u, 64u, 896u, 640u, 0x00161b22u};
    osaura_jx11_fill_request bar = {96u, 112u, 832u, 72u, 0x002c7be5u};
    osaura_jx11_fill_request left = {96u, 216u, 384u, 424u, 0x00202731u};
    osaura_jx11_fill_request right = {512u, 216u, 416u, 424u, 0x00303945u};
    int rc = osaura_jx11_display_clear(0x000b0e12u);
    if (rc != 0) return rc;
    if ((rc = osaura_jx11_display_fill(&panel)) != 0) return rc;
    if ((rc = osaura_jx11_display_fill(&bar)) != 0) return rc;
    if ((rc = osaura_jx11_display_fill(&left)) != 0) return rc;
    if ((rc = osaura_jx11_display_fill(&right)) != 0) return rc;
    return osaura_windows_display_present();
}

static int vfs_test(void) {
    static const char payload[] = "WSJX64-VFS64";
    char readback[32] = {0};
    uint32_t h = OSAURA_WINDOWS_VFS64_NONE, n = 0u;
    int rc = osaura_windows_vfs64_open_as(OSAURA_SECURITY_KERNEL_SUBJECT,
        "/vfs64-test.bin", OSAURA_WINDOWS_VFS64_WRITE, &h);
    if (rc != 0) return rc;
    rc = osaura_windows_vfs64_write_as(OSAURA_SECURITY_KERNEL_SUBJECT, h,
                                       payload, (uint32_t)sizeof payload, &n);
    if (osaura_windows_vfs64_close_as(OSAURA_SECURITY_KERNEL_SUBJECT, h) != 0 && rc == 0) rc = -20;
    if (rc != 0 || n != sizeof payload) return rc ? rc : -21;
    rc = osaura_windows_vfs64_open_as(OSAURA_SECURITY_JX_SUBJECT,
        "/vfs64-test.bin", OSAURA_WINDOWS_VFS64_READ, &h);
    if (rc != 0) return rc;
    rc = osaura_windows_vfs64_read_as(OSAURA_SECURITY_JX_SUBJECT, h,
                                      readback, (uint32_t)sizeof payload, &n);
    if (osaura_windows_vfs64_close_as(OSAURA_SECURITY_JX_SUBJECT, h) != 0 && rc == 0) rc = -22;
    if (rc != 0 || n != sizeof payload || memcmp(readback, payload, sizeof payload) != 0) return rc ? rc : -23;
    return 0;
}

static int load_module(wsjx_state *state, const char *jx_path) {
    uint32_t h = OSAURA_WINDOWS_VFS64_NONE;
    int rc = osaura_windows_vfs64_open_as(OSAURA_SECURITY_JX_SUBJECT,
        jx_path, OSAURA_WINDOWS_VFS64_READ, &h);
    if (rc != 0) return rc;
    osaura_windows_vfs64_info info;
    rc = osaura_windows_vfs64_stat_as(OSAURA_SECURITY_JX_SUBJECT, h, &info);
    unsigned char head[16] = {0};
    uint32_t got = 0u;
    if (rc == 0) rc = osaura_windows_vfs64_read_as(OSAURA_SECURITY_JX_SUBJECT,
                                                    h, head, sizeof head, &got);
    int close_rc = osaura_windows_vfs64_close_as(OSAURA_SECURITY_JX_SUBJECT, h);
    if (rc == 0 && close_rc != 0) rc = close_rc;
    if (rc != 0 || got == 0u || info.size > UINT32_MAX) return rc ? rc : -5;
    if (copy_path(state->loaded_path, sizeof state->loaded_path, jx_path) != 0) return -6;
    state->loaded_bytes = (uint32_t)info.size;
    printf("LOADED VIA VFS64: %s\n", state->loaded_path);
    printf("BYTES: %u\n", state->loaded_bytes);
    printf("HEADER:");
    for (uint32_t i = 0u; i < got; ++i) printf(" %02X", head[i]);
    puts("");
    return 0;
}

static int memory_test(void) {
    uint32_t id = OSAURA_WINDOWS_MEMORY64_NONE;
    int rc = osaura_windows_memory64_alloc_as(OSAURA_SECURITY_JX_SUBJECT, 65536u, &id);
    if (rc != 0) return rc;
    uint8_t *p = (uint8_t *)osaura_windows_memory64_map_as(OSAURA_SECURITY_JX_SUBJECT, id);
    if (!p) { (void)osaura_windows_memory64_free_as(OSAURA_SECURITY_JX_SUBJECT, id); return -10; }
    p[0] = 0x4au; p[65535] = 0x58u;
    osaura_windows_memory64_info info;
    rc = osaura_windows_memory64_info_as(OSAURA_SECURITY_JX_SUBJECT, id, &info);
    if (rc == 0 && (info.bytes != 65536u || p[0] != 0x4au || p[65535] != 0x58u)) rc = -11;
    int free_rc = osaura_windows_memory64_free_as(OSAURA_SECURITY_JX_SUBJECT, id);
    return rc != 0 ? rc : free_rc;
}

static void print_tasks(void) {
    uint32_t count = osaura_windows_task64_count();
    printf("TASK64 COUNT: %u FOREGROUND: %u BACKGROUND: %u\n", count,
           osaura_windows_job64_foreground(), osaura_windows_job64_background_count());
    for (uint32_t i = 0u; i < count; ++i) {
        osaura_windows_task64_info info;
        if (osaura_windows_task64_info(i, &info) == 0)
            printf("%u SUBJECT %u ROLE %u STATE %u %s\n", info.task_id,
                   info.subject, (unsigned)info.role, (unsigned)info.state, info.name);
    }
}

static int ipc_test(void) {
    uint32_t channel = OSAURA_IPC_NONE;
    static const char text[] = "JX64-IPC64";
    osaura_ipc_message message;
    int rc = osaura_windows_ipc64_create(2u, &channel);
    if (rc != 0) return rc;
    rc = osaura_windows_ipc64_send(1u, channel, 64u, text, (uint32_t)sizeof text);
    if (rc == 0 && osaura_windows_ipc64_pending(channel) != 1u) rc = -10;
    if (rc == 0) rc = osaura_windows_ipc64_receive(2u, channel, &message);
    if (rc == 1) rc = (message.bytes == sizeof text &&
                       memcmp(message.payload, text, sizeof text) == 0) ? 0 : -11;
    else if (rc == 0) rc = -12;
    int close_rc = osaura_windows_ipc64_close(2u, channel);
    return rc != 0 ? rc : close_rc;
}

static void print_status(const wsjx_state *state) {
    puts("ARCH: AMD64 / 64-BIT ONLY");
    printf("MODE: %s\n", state->jxl_mode ? "JXL" : "JX");
    printf("NETWORK: %s\n", state->socket_ready ? "WINSOCK64 READY" : "UNAVAILABLE");
    printf("DISPLAY: %s\n", state->display_ready ? "JX11 USER64/GDI64 READY" : "UNAVAILABLE");
    printf("VFS64: %s\n", state->vfs_ready ? "READY" : "UNAVAILABLE");
    printf("CLOCK64: %s\n", state->clock_ready ? "READY" : "UNAVAILABLE");
    printf("MEMORY64: %s\n", state->memory_ready ? "READY" : "UNAVAILABLE");
    printf("INPUT64: %s\n", state->input_ready ? "READY" : "UNAVAILABLE");
    printf("TASK64/JOB64: %s\n", state->task_ready ? "READY" : "UNAVAILABLE");
    printf("IPC64: %s\n", state->ipc_ready ? "READY" : "UNAVAILABLE");
    printf("SECURITY GENERATION: %llu\n", (unsigned long long)osaura_security_generation());
    if (state->loaded_bytes)
        printf("MODULE: %s (%u bytes)\n", state->loaded_path, state->loaded_bytes);
    else
        puts("MODULE: NONE");
}

static void clear_console(void) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    DWORD written = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &info)) {
        DWORD cells = (DWORD)info.dwSize.X * (DWORD)info.dwSize.Y;
        COORD home = {0, 0};
        FillConsoleOutputCharacterA(h, ' ', cells, home, &written);
        FillConsoleOutputAttribute(h, info.wAttributes, cells, home, &written);
        SetConsoleCursorPosition(h, home);
    }
}

static int make_vfs_root(char *out, size_t capacity) {
    DWORD n = GetCurrentDirectoryA((DWORD)capacity, out);
    if (n == 0u || n >= capacity) return -1;
    static const char suffix[] = "\\wsjx-root";
    size_t at = strlen(out), add = sizeof suffix;
    if (at + add > capacity) return -2;
    memcpy(out + at, suffix, add);
    return 0;
}

int main(int argc, char **argv) {
    wsjx_state state;
    memset(&state, 0, sizeof state);

    osaura_security_init();
    state.socket_ready = osaura_windows_socket_backend_install() == 0 ? 1u : 0u;
    state.display_ready = osaura_windows_display_backend_install(
        WSJX_DISPLAY_WIDTH, WSJX_DISPLAY_HEIGHT) == 0 ? 1u : 0u;
    char vfs_root[MAX_PATH];
    state.vfs_ready = make_vfs_root(vfs_root, sizeof vfs_root) == 0 &&
        osaura_windows_vfs64_init(vfs_root) == 0 ? 1u : 0u;
    state.clock_ready = osaura_windows_clock64_init() == 0 ? 1u : 0u;
    state.memory_ready = osaura_windows_memory64_init() == 0 ? 1u : 0u;
    state.task_ready = osaura_windows_task64_init() == 0 ? 1u : 0u;
    state.ipc_ready = osaura_windows_ipc64_init() == 0 ? 1u : 0u;
    state.input_ready = osaura_windows_input64_init() == 0 ? 1u : 0u;

    print_banner(&state);

    if (argc > 1) {
        int rc = state.vfs_ready ? load_module(&state, argv[1]) : -1;
        if (rc != 0) {
            fprintf(stderr, "WSJX64: failed to load VFS module (%d): %s\n", rc, argv[1]);
            osaura_windows_display_shutdown();
            return 2;
        }
    }

    char line[LINE_BYTES];
    for (;;) {
        (void)osaura_windows_display_pump();
        fputs("WSJX64> ", stdout);
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        trim_line(line);
        if (!line[0]) continue;

        if (text_equal(line, "HELP")) print_help();
        else if (text_equal(line, "ABOUT")) print_banner(&state);
        else if (text_equal(line, "PLATFORM")) print_platform();
        else if (text_equal(line, "STATUS")) print_status(&state);
        else if (text_equal(line, "CAPS")) print_caps();
        else if (text_equal(line, "MODE JX")) { state.jxl_mode = 0u; puts("MODE: JX"); }
        else if (text_equal(line, "MODE JXL")) { state.jxl_mode = 1u; puts("MODE: JXL"); }
        else if (text_equal(line, "SOCKET")) {
            int rc = state.socket_ready ? socket_self_test() : -1;
            printf("JX SOCKET: %s", rc == 0 ? "PASS" : "FAIL");
            if (rc != 0) printf(" (%d)", rc);
            puts("");
        }
        else if (text_equal(line, "MOUNTS"))
            printf("/ -> %s\n", state.vfs_ready ? osaura_windows_vfs64_root() : "UNAVAILABLE");
        else if (text_equal(line, "VFS TEST")) {
            int rc = state.vfs_ready ? vfs_test() : -1;
            printf("VFS64 TEST: %s", rc == 0 ? "PASS" : "FAIL");
            if (rc != 0) printf(" (%d)", rc);
            puts("");
        }
        else if (starts_with(line, "LOAD ")) {
            const char *path = line + 5;
            while (*path == ' ' || *path == '\t') ++path;
            int rc = state.vfs_ready ? load_module(&state, path) : -1;
            if (rc != 0) printf("LOAD FAILED: %d\n", rc);
        }
        else if (text_equal(line, "UNLOAD")) {
            state.loaded_bytes = 0u;
            state.loaded_path[0] = 0;
            puts("MODULE: NONE");
        }
        else if (text_equal(line, "CLOCK"))
            printf("CLOCK64 TICKS: %llu MS: %llu\n",
                   (unsigned long long)osaura_windows_clock64_ticks(),
                   (unsigned long long)osaura_windows_clock64_ms());
        else if (text_equal(line, "MEM TEST")) {
            int rc = state.memory_ready ? memory_test() : -1;
            printf("MEMORY64 TEST: %s", rc == 0 ? "PASS" : "FAIL");
            if (rc != 0) printf(" (%d)", rc);
            puts("");
        }
        else if (text_equal(line, "TASKS")) print_tasks();
        else if (text_equal(line, "IPC TEST")) {
            int rc = state.ipc_ready ? ipc_test() : -1;
            printf("IPC64 TEST: %s", rc == 0 ? "PASS" : "FAIL");
            if (rc != 0) printf(" (%d)", rc);
            puts("");
        }
        else if (text_equal(line, "INPUT"))
            printf("INPUT64: %s\n", osaura_windows_input64_is_console() ? "CONSOLE EVENTS" : "REDIRECTED/PIPE MODE");
        else if (text_equal(line, "DISPLAY")) {
            int rc = state.display_ready ? display_info() : -1;
            if (rc != 0) printf("JX11 DISPLAY: FAIL (%d)\n", rc);
        }
        else if (text_equal(line, "DISPLAY OPEN")) {
            int rc = state.display_ready ? osaura_windows_display_show() : -1;
            printf("JX11 DISPLAY OPEN: %s", rc == 0 ? "PASS" : "FAIL");
            if (rc != 0) printf(" (%d)", rc);
            puts("");
        }
        else if (text_equal(line, "DISPLAY TEST")) {
            int rc = state.display_ready ? display_test() : -1;
            printf("JX11 DISPLAY TEST: %s", rc == 0 ? "PASS" : "FAIL");
            if (rc != 0) printf(" (%d)", rc);
            puts("");
        }
        else if (text_equal(line, "CLEAR")) clear_console();
        else if (text_equal(line, "EXIT") || text_equal(line, "HALT")) break;
        else puts("UNKNOWN COMMAND - TYPE HELP");

        (void)osaura_windows_display_pump();
    }

    osaura_windows_display_shutdown();
    return 0;
}

#endif
