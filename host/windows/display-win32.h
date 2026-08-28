#ifndef OSAURA_WINDOWS_DISPLAY_WIN32_H
#define OSAURA_WINDOWS_DISPLAY_WIN32_H

#include <stdint.h>

int osaura_windows_display_backend_install(uint32_t width, uint32_t height);
int osaura_windows_display_present(void);
int osaura_windows_display_show(void);
int osaura_windows_display_pump(void);
void osaura_windows_display_shutdown(void);

#endif
