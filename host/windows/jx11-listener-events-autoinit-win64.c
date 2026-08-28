#ifdef _WIN64

#include "../../runtime/jx/jx11-listener-events.h"

static int g_listener_events_status = -1;

static void __cdecl osaura_windows_jx11_listener_events_crt_init(void) {
    g_listener_events_status = osaura_jx11_listener_events_init();
}

#if defined(_MSC_VER)
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) void (__cdecl *osaura_windows_jx11_listener_events_initializer)(void) =
    osaura_windows_jx11_listener_events_crt_init;
#pragma comment(linker, "/include:osaura_windows_jx11_listener_events_initializer")
#else
__attribute__((constructor)) static void osaura_windows_jx11_listener_events_constructor(void) {
    osaura_windows_jx11_listener_events_crt_init();
}
#endif

int osaura_windows_jx11_listener_events_autoinit_status(void) {
    return g_listener_events_status;
}

#endif
