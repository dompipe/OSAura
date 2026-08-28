#ifdef _WIN32

#include "../../include/osaura/socket.h"
#include "../../kernel/security.h"
#include "../../runtime/jx/jx11-display.h"
#include "socket-winsock.h"
#include "display-win32.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define WSJX_VERSION "0.2-dev"
#define LINE_BYTES 512u
#define WSJX_DISPLAY_WIDTH 1024u
#define WSJX_DISPLAY_HEIGHT 768u

typedef struct {
    uint8_t jxl_mode;
    uint8_t socket_ready;
    uint8_t display_ready;
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

static void print_banner(const wsjx_state *state) {
    puts("WSJX - WINDOWS SUBSYSTEM FOR JX");
    printf("VERSION: %s\n", WSJX_VERSION);
    puts("JX HOT ABI V4: ACTIVE");
    printf("MODE: %s\n", state->jxl_mode ? "JXL" : "JX");
    puts("SECURITY SUBJECT 1: ACTIVE");
    printf("NETWORK: %s\n", state->socket_ready ? "WINSOCK BACKEND" : "UNAVAILABLE");
    printf("JX11 DISPLAY: %s\n", state->display_ready ? "WIN32 DIB BACKEND" : "UNAVAILABLE");
    puts("F0-FF: RESERVED");
    puts("");
}

static void print_help(void) {
    puts("HELP                 show commands");
    puts("ABOUT                show WSJX identity");
    puts("STATUS               show subsystem state");
    puts("MODE JX              select normal JX session mode");
    puts("MODE JXL             select JXL session mode");
    puts("CAPS                 show JX subject capabilities");
    puts("SOCKET               open/close a JX TCP socket through Winsock");
    puts("LOAD <path>          load a binary module into WSJX state");
    puts("UNLOAD               release loaded module state");
    puts("DISPLAY              show JX11 display geometry");
    puts("DISPLAY OPEN         show the JX11 Windows surface");
    puts("DISPLAY TEST         draw through JX11 and present");
    puts("CLEAR                clear the console");
    puts("EXIT                 leave WSJX");
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

static int load_module(wsjx_state *state, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -2; }
    long size = ftell(f);
    if (size <= 0 || size > (64L * 1024L * 1024L)) { fclose(f); return -3; }
    rewind(f);

    unsigned char head[16] = {0};
    size_t got = fread(head, 1, sizeof head, f);
    fclose(f);
    if (!got) return -4;
    if (copy_path(state->loaded_path, sizeof state->loaded_path, path) != 0) return -5;

    state->loaded_bytes = (uint32_t)size;

    printf("LOADED: %s\n", state->loaded_path);
    printf("BYTES: %u\n", state->loaded_bytes);
    printf("HEADER:");
    for (size_t i = 0; i < got; ++i) printf(" %02X", head[i]);
    puts("");
    return 0;
}

static void print_status(const wsjx_state *state) {
    printf("MODE: %s\n", state->jxl_mode ? "JXL" : "JX");
    printf("NETWORK: %s\n", state->socket_ready ? "READY" : "UNAVAILABLE");
    printf("DISPLAY: %s\n", state->display_ready ? "JX11 WIN32 READY" : "UNAVAILABLE");
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

int main(int argc, char **argv) {
    wsjx_state state;
    memset(&state, 0, sizeof state);

    osaura_security_init();
    state.socket_ready = osaura_windows_socket_backend_install() == 0 ? 1u : 0u;
    state.display_ready = osaura_windows_display_backend_install(
        WSJX_DISPLAY_WIDTH, WSJX_DISPLAY_HEIGHT) == 0 ? 1u : 0u;

    print_banner(&state);

    if (argc > 1) {
        int rc = load_module(&state, argv[1]);
        if (rc != 0) {
            fprintf(stderr, "WSJX: failed to load module (%d): %s\n", rc, argv[1]);
            osaura_windows_display_shutdown();
            return 2;
        }
    }

    char line[LINE_BYTES];
    for (;;) {
        (void)osaura_windows_display_pump();
        fputs("WSJX> ", stdout);
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        trim_line(line);
        if (!line[0]) continue;

        if (text_equal(line, "HELP")) print_help();
        else if (text_equal(line, "ABOUT")) print_banner(&state);
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
        else if (starts_with(line, "LOAD ")) {
            const char *path = line + 5;
            while (*path == ' ' || *path == '\t') ++path;
            int rc = load_module(&state, path);
            if (rc != 0) printf("LOAD FAILED: %d\n", rc);
        }
        else if (text_equal(line, "UNLOAD")) {
            state.loaded_bytes = 0u;
            state.loaded_path[0] = 0;
            puts("MODULE: NONE");
        }
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
