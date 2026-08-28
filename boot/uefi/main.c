#include <efi.h>
#include <efilib.h>

#define OSAURA_LINE_MAX 128

static EFI_SYSTEM_TABLE *g_st;

static void print_prompt(void) {
    Print(L"osaura> ");
}

static void print_banner(void) {
    Print(L"\r\nOSAura 0.1-dev\r\n");
    Print(L"x86_64 UEFI terminal\r\n");
    Print(L"Type 'help' for commands.\r\n\r\n");
}

static void print_memory_summary(void) {
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;

    EFI_STATUS status = uefi_call_wrapper(
        g_st->BootServices->GetMemoryMap,
        5,
        &map_size,
        NULL,
        &map_key,
        &descriptor_size,
        &descriptor_version);

    if (status != EFI_BUFFER_TOO_SMALL || descriptor_size == 0) {
        Print(L"memory map unavailable: %r\r\n", status);
        return;
    }

    UINTN descriptors = map_size / descriptor_size;
    Print(L"UEFI memory map requires %lu bytes (%lu descriptors, descriptor size %lu).\r\n",
          map_size,
          descriptors,
          descriptor_size);
}

static UINTN read_line(CHAR16 *buffer, UINTN capacity) {
    UINTN length = 0;
    if (!buffer || capacity < 2) return 0;

    for (;;) {
        UINTN event_index = 0;
        EFI_INPUT_KEY key;
        EFI_STATUS status = uefi_call_wrapper(
            g_st->BootServices->WaitForEvent,
            3,
            1,
            &g_st->ConIn->WaitForKey,
            &event_index);
        if (EFI_ERROR(status)) continue;

        status = uefi_call_wrapper(g_st->ConIn->ReadKeyStroke, 2, g_st->ConIn, &key);
        if (EFI_ERROR(status)) continue;

        if (key.UnicodeChar == L'\r') {
            Print(L"\r\n");
            break;
        }

        if (key.UnicodeChar == L'\b') {
            if (length > 0) {
                --length;
                Print(L"\b \b");
            }
            continue;
        }

        if (key.UnicodeChar >= L' ' && key.UnicodeChar <= L'~' && length + 1 < capacity) {
            buffer[length++] = key.UnicodeChar;
            Print(L"%c", key.UnicodeChar);
        }
    }

    buffer[length] = L'\0';
    return length;
}

static void run_command(const CHAR16 *line) {
    if (!line || line[0] == L'\0') return;

    if (StrCmp((CHAR16 *)line, L"help") == 0) {
        Print(L"help   - show commands\r\n");
        Print(L"about  - describe this build\r\n");
        Print(L"mem    - query the UEFI memory map\r\n");
        Print(L"clear  - clear the terminal\r\n");
        Print(L"reboot - reboot through UEFI\r\n");
        return;
    }

    if (StrCmp((CHAR16 *)line, L"about") == 0) {
        Print(L"OSAura terminal-first operating system bootstrap.\r\n");
        Print(L"JX runtime integration follows the kernel/UEFI boundary.\r\n");
        return;
    }

    if (StrCmp((CHAR16 *)line, L"mem") == 0) {
        print_memory_summary();
        return;
    }

    if (StrCmp((CHAR16 *)line, L"clear") == 0) {
        uefi_call_wrapper(g_st->ConOut->ClearScreen, 1, g_st->ConOut);
        return;
    }

    if (StrCmp((CHAR16 *)line, L"reboot") == 0) {
        uefi_call_wrapper(g_st->RuntimeServices->ResetSystem,
                          4,
                          EfiResetCold,
                          EFI_SUCCESS,
                          0,
                          NULL);
        return;
    }

    Print(L"unknown command: %s\r\n", line);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    (void)image_handle;
    InitializeLib(image_handle, system_table);
    g_st = system_table;

    uefi_call_wrapper(g_st->ConOut->Reset, 2, g_st->ConOut, FALSE);
    uefi_call_wrapper(g_st->ConOut->ClearScreen, 1, g_st->ConOut);

    print_banner();
    print_memory_summary();

    CHAR16 line[OSAURA_LINE_MAX];
    for (;;) {
        print_prompt();
        read_line(line, OSAURA_LINE_MAX);
        run_command(line);
    }

    return EFI_SUCCESS;
}
