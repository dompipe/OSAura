#ifdef _WIN64

#include "processor-bus-autobind-win64.h"
#include "processor-bus-win64.h"

static int g_bus_bind_status = -1;

static void __cdecl osaura_windows_processor_bus64_crt_bind(void) {
    g_bus_bind_status = osaura_windows_processor_bus64_bind();
}

#if defined(_MSC_VER)
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void (__cdecl *osaura_windows_processor_bus64_initializer)(void) =
    osaura_windows_processor_bus64_crt_bind;
#pragma comment(linker, "/include:osaura_windows_processor_bus64_initializer")
#else
__attribute__((constructor)) static void osaura_windows_processor_bus64_constructor(void) {
    osaura_windows_processor_bus64_crt_bind();
}
#endif

int osaura_windows_processor_bus64_autobind_status(void) {
    return g_bus_bind_status;
}

#endif
