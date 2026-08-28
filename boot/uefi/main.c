#include <efi.h>
#include <efilib.h>

#include "../../kernel/boot-info.h"

extern void osaura_kernel_main(const osaura_boot_info *boot);

static EFI_GUID g_gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

static EFI_STATUS locate_framebuffer(EFI_SYSTEM_TABLE *st, osaura_boot_info *boot) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS status = uefi_call_wrapper(
        st->BootServices->LocateProtocol,
        3,
        &g_gop_guid,
        NULL,
        (VOID **)&gop);
    if (EFI_ERROR(status) || !gop || !gop->Mode || !gop->Mode->Info)
        return EFI_NOT_FOUND;

    boot->width = gop->Mode->Info->HorizontalResolution;
    boot->height = gop->Mode->Info->VerticalResolution;
    boot->pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;
    boot->framebuffer_base = (uint64_t)gop->Mode->FrameBufferBase;
    boot->framebuffer_size = (uint64_t)gop->Mode->FrameBufferSize;

    switch (gop->Mode->Info->PixelFormat) {
        case PixelRedGreenBlueReserved8BitPerColor:
            boot->pixel_format = OSAURA_PIXEL_RGBX8;
            break;
        case PixelBlueGreenRedReserved8BitPerColor:
            boot->pixel_format = OSAURA_PIXEL_BGRX8;
            break;
        default:
            boot->pixel_format = OSAURA_PIXEL_UNKNOWN;
            return EFI_UNSUPPORTED;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS load_jx_book(EFI_HANDLE image_handle,
                               EFI_SYSTEM_TABLE *st,
                               osaura_boot_info *boot) {
    EFI_LOADED_IMAGE *loaded_image = NULL;
    EFI_STATUS status = uefi_call_wrapper(
        st->BootServices->HandleProtocol,
        3,
        image_handle,
        &LoadedImageProtocol,
        (VOID **)&loaded_image);
    if (EFI_ERROR(status) || !loaded_image || !loaded_image->DeviceHandle)
        return EFI_NOT_FOUND;

    EFI_FILE_HANDLE root = LibOpenRoot(loaded_image->DeviceHandle);
    if (!root) return EFI_NOT_FOUND;

    EFI_FILE_HANDLE file = NULL;
    status = uefi_call_wrapper(
        root->Open,
        5,
        root,
        &file,
        L"\\OSAURA\\runtime.64B",
        EFI_FILE_MODE_READ,
        0);
    if (EFI_ERROR(status) || !file) {
        uefi_call_wrapper(root->Close, 1, root);
        return EFI_NOT_FOUND;
    }

    EFI_FILE_INFO *info = LibFileInfo(file);
    if (!info || info->FileSize == 0 || info->FileSize > (64ull << 20)) {
        if (info) uefi_call_wrapper(st->BootServices->FreePool, 1, info);
        uefi_call_wrapper(file->Close, 1, file);
        uefi_call_wrapper(root->Close, 1, root);
        return EFI_BAD_BUFFER_SIZE;
    }

    UINTN bytes = (UINTN)info->FileSize;
    VOID *book = NULL;
    status = uefi_call_wrapper(
        st->BootServices->AllocatePool,
        3,
        EfiLoaderData,
        bytes,
        &book);
    if (!EFI_ERROR(status)) {
        UINTN read_bytes = bytes;
        status = uefi_call_wrapper(file->Read, 3, file, &read_bytes, book);
        if (!EFI_ERROR(status) && read_bytes != bytes) status = EFI_DEVICE_ERROR;
    }

    uefi_call_wrapper(st->BootServices->FreePool, 1, info);
    uefi_call_wrapper(file->Close, 1, file);
    uefi_call_wrapper(root->Close, 1, root);

    if (EFI_ERROR(status)) {
        if (book) uefi_call_wrapper(st->BootServices->FreePool, 1, book);
        return status;
    }

    boot->jx_book = (uint64_t)(uintptr_t)book;
    boot->jx_book_size = (uint64_t)bytes;
    return EFI_SUCCESS;
}

static EFI_STATUS capture_memory_map(EFI_SYSTEM_TABLE *st,
                                     EFI_MEMORY_DESCRIPTOR **map_out,
                                     UINTN *capacity_out,
                                     UINTN *map_size_out,
                                     UINTN *map_key_out,
                                     UINTN *descriptor_size_out,
                                     UINT32 *descriptor_version_out) {
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;

    EFI_STATUS status = uefi_call_wrapper(
        st->BootServices->GetMemoryMap,
        5,
        &map_size,
        NULL,
        &map_key,
        &descriptor_size,
        &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL || descriptor_size == 0)
        return status;

    UINTN capacity = map_size + descriptor_size * 8u;
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    status = uefi_call_wrapper(
        st->BootServices->AllocatePool,
        3,
        EfiLoaderData,
        capacity,
        (VOID **)&map);
    if (EFI_ERROR(status)) return status;

    map_size = capacity;
    status = uefi_call_wrapper(
        st->BootServices->GetMemoryMap,
        5,
        &map_size,
        map,
        &map_key,
        &descriptor_size,
        &descriptor_version);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(st->BootServices->FreePool, 1, map);
        return status;
    }

    *map_out = map;
    *capacity_out = capacity;
    *map_size_out = map_size;
    *map_key_out = map_key;
    *descriptor_size_out = descriptor_size;
    *descriptor_version_out = descriptor_version;
    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    InitializeLib(image_handle, system_table);

    uefi_call_wrapper(system_table->ConOut->Reset, 2, system_table->ConOut, FALSE);
    uefi_call_wrapper(system_table->ConOut->ClearScreen, 1, system_table->ConOut);
    Print(L"\r\nOSAura loader 0.3-dev\r\n");
    Print(L"Preparing native kernel handoff...\r\n");

    osaura_boot_info boot;
    SetMem(&boot, sizeof boot, 0);
    boot.version = OSAURA_BOOT_INFO_VERSION;

    EFI_STATUS status = locate_framebuffer(system_table, &boot);
    if (EFI_ERROR(status)) {
        Print(L"GOP framebuffer unavailable: %r\r\n", status);
        return status;
    }

    status = load_jx_book(image_handle, system_table, &boot);
    if (EFI_ERROR(status)) {
        Print(L"JX runtime Book unavailable: %r\r\n", status);
        return status;
    }
    Print(L"JX runtime Book loaded: %lu bytes\r\n", boot.jx_book_size);

    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN map_capacity = 0;
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;

    status = capture_memory_map(system_table,
                                &map,
                                &map_capacity,
                                &map_size,
                                &map_key,
                                &descriptor_size,
                                &descriptor_version);
    if (EFI_ERROR(status)) {
        Print(L"Memory map capture failed: %r\r\n", status);
        return status;
    }

    status = uefi_call_wrapper(system_table->BootServices->ExitBootServices,
                               2,
                               image_handle,
                               map_key);

    if (status == EFI_INVALID_PARAMETER) {
        map_size = map_capacity;
        status = uefi_call_wrapper(system_table->BootServices->GetMemoryMap,
                                   5,
                                   &map_size,
                                   map,
                                   &map_key,
                                   &descriptor_size,
                                   &descriptor_version);
        if (!EFI_ERROR(status)) {
            status = uefi_call_wrapper(system_table->BootServices->ExitBootServices,
                                       2,
                                       image_handle,
                                       map_key);
        }
    }

    if (EFI_ERROR(status)) return status;

    boot.memory_map = (uint64_t)(uintptr_t)map;
    boot.memory_map_size = (uint64_t)map_size;
    boot.memory_descriptor_size = (uint64_t)descriptor_size;
    boot.memory_descriptor_version = descriptor_version;

    osaura_kernel_main(&boot);
    return EFI_SUCCESS;
}
