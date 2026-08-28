#ifdef _WIN64

#include "jx11-surface-win64.h"
#include "../../runtime/jx/jx11-surface.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

static void *surface_alloc(size_t bytes, void *context) {
    (void)context;
    if (!bytes) return 0;
    return VirtualAlloc(0, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

static void surface_free(void *ptr, size_t bytes, void *context) {
    (void)bytes;
    (void)context;
    if (ptr) (void)VirtualFree(ptr, 0, MEM_RELEASE);
}

int osaura_windows_jx11_surface_install(void) {
    return osaura_jx11_surface_init(surface_alloc, surface_free, 0);
}

void osaura_windows_jx11_surface_shutdown(void) {
    osaura_jx11_surface_shutdown();
}

#endif
