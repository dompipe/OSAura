#ifdef _WIN32

#include "display-win32.h"
#include "../../kernel/display.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

static HINSTANCE g_instance;
static HWND g_window;
static HDC g_memory_dc;
static HBITMAP g_bitmap;
static HGDIOBJ g_old_bitmap;
static void *g_pixels;
static uint32_t g_width;
static uint32_t g_height;
static uint8_t g_class_ready;

static const char *g_class_name = "WSJXDisplayWindow";

static LRESULT CALLBACK display_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    (void)wparam;
    (void)lparam;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (dc && g_memory_dc && g_width && g_height)
            BitBlt(dc, 0, 0, (int)g_width, (int)g_height, g_memory_dc, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_CLOSE) {
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

static int ensure_window_class(void) {
    if (g_class_ready) return 0;
    g_instance = GetModuleHandleA(0);
    if (!g_instance) return -10;

    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = display_wndproc;
    wc.hInstance = g_instance;
    wc.hCursor = LoadCursorA(0, IDC_ARROW);
    wc.lpszClassName = g_class_name;

    if (!RegisterClassA(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) return -(int)err;
    }
    g_class_ready = 1u;
    return 0;
}

static int create_surface(uint32_t width, uint32_t height) {
    BITMAPINFO info;
    ZeroMemory(&info, sizeof info);
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = (LONG)width;
    info.bmiHeader.biHeight = -(LONG)height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(0);
    if (!screen) return -20;
    g_memory_dc = CreateCompatibleDC(screen);
    if (!g_memory_dc) {
        ReleaseDC(0, screen);
        return -21;
    }

    g_bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &g_pixels, 0, 0);
    ReleaseDC(0, screen);
    if (!g_bitmap || !g_pixels) return -22;

    g_old_bitmap = SelectObject(g_memory_dc, g_bitmap);
    if (!g_old_bitmap || g_old_bitmap == HGDI_ERROR) return -23;

    g_width = width;
    g_height = height;

    osaura_display_surface surface;
    surface.width = width;
    surface.height = height;
    surface.stride_pixels = width;
    surface.pixel_format = OSAURA_DISPLAY_PIXEL_BGRX8;
    surface.framebuffer_base = (uint64_t)(uintptr_t)g_pixels;
    surface.framebuffer_size = (uint64_t)width * (uint64_t)height * sizeof(uint32_t);
    return osaura_display_init_surface(&surface);
}

static int create_window(void) {
    RECT rect = {0, 0, (LONG)g_width, (LONG)g_height};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    if (!AdjustWindowRect(&rect, style, FALSE)) return -30;

    g_window = CreateWindowExA(0,
                               g_class_name,
                               "JX11 / WSJX",
                               style,
                               CW_USEDEFAULT,
                               CW_USEDEFAULT,
                               rect.right - rect.left,
                               rect.bottom - rect.top,
                               0,
                               0,
                               g_instance,
                               0);
    return g_window ? 0 : -(int)GetLastError();
}

int osaura_windows_display_backend_install(uint32_t width, uint32_t height) {
    if (!width || !height || width > 8192u || height > 8192u) return -1;
    osaura_windows_display_shutdown();
    if (ensure_window_class() != 0) return -2;
    if (create_surface(width, height) != 0) {
        osaura_windows_display_shutdown();
        return -3;
    }
    if (create_window() != 0) {
        osaura_windows_display_shutdown();
        return -4;
    }
    return 0;
}

int osaura_windows_display_present(void) {
    if (!g_window || !g_memory_dc) return -1;
    InvalidateRect(g_window, 0, FALSE);
    UpdateWindow(g_window);
    return 0;
}

int osaura_windows_display_show(void) {
    if (!g_window) return -1;
    ShowWindow(g_window, SW_SHOW);
    return osaura_windows_display_present();
}

int osaura_windows_display_pump(void) {
    MSG msg;
    int count = 0;
    while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
        ++count;
    }
    return count;
}

void osaura_windows_display_shutdown(void) {
    if (g_window) {
        DestroyWindow(g_window);
        g_window = 0;
    }
    if (g_memory_dc && g_old_bitmap && g_old_bitmap != HGDI_ERROR) {
        SelectObject(g_memory_dc, g_old_bitmap);
        g_old_bitmap = 0;
    }
    if (g_bitmap) {
        DeleteObject(g_bitmap);
        g_bitmap = 0;
    }
    if (g_memory_dc) {
        DeleteDC(g_memory_dc);
        g_memory_dc = 0;
    }
    g_pixels = 0;
    g_width = 0u;
    g_height = 0u;
}

#endif
