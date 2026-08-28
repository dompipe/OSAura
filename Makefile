BUILD := build
EFI := $(BUILD)/BOOTX64.EFI
LOADER_OBJ := $(BUILD)/loader.o
KERNEL_OBJ := $(BUILD)/kernel.o
MM_OBJ := $(BUILD)/mm.o
SCHED_OBJ := $(BUILD)/scheduler.o
USB_OBJ := $(BUILD)/usb.o
USB_HOT_OBJ := $(BUILD)/usb-hot.o
E1000_OBJ := $(BUILD)/e1000.o
NET_OBJ := $(BUILD)/net.o
WIFI_OBJ := $(BUILD)/wifi.o
WIFI_HOT_OBJ := $(BUILD)/wifi-hot.o
CLOCK_HOT_OBJ := $(BUILD)/clock-hot.o
MEMORY_HOT_OBJ := $(BUILD)/memory-hot.o
TASK_HOT_OBJ := $(BUILD)/task-hot.o
VFS_OBJ := $(BUILD)/vfs.o
BOOK_HOT_OBJ := $(BUILD)/book-hot.o
SECURITY_OBJ := $(BUILD)/security.o
HOT_SHADOW_OBJ := $(BUILD)/hot-shadow.o
STORAGE_OBJ := $(BUILD)/storage.o
IPC_OBJ := $(BUILD)/ipc.o
JX_RUNTIME_OBJ := $(BUILD)/jx-runtime.o
JX_LIVE_OBJ := $(BUILD)/jx-live.o
ARCH_OBJ := $(BUILD)/x86_64.o
SO := $(BUILD)/bootx64.so

CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy

EFI_INC := /usr/include/efi
EFI_ARCH_INC := /usr/include/efi/x86_64
EFI_LDS := /usr/lib/elf_x86_64_efi.lds
EFI_CRT := /usr/lib/crt0-efi-x86_64.o

COMMON_FLAGS := -fpic -ffreestanding -fno-stack-protector -fno-stack-check \
	-mno-red-zone -Wall -Wextra -Werror -O2
LOADER_CFLAGS := $(COMMON_FLAGS) -fshort-wchar -maccumulate-outgoing-args \
	-I$(EFI_INC) -I$(EFI_ARCH_INC) -I$(EFI_INC)/protocol
KERNEL_CFLAGS := $(COMMON_FLAGS) -fcf-protection=none -mgeneral-regs-only \
	-Ikernel -Iruntime/jx

LDFLAGS := -nostdlib -znocombreloc -T $(EFI_LDS) -shared -Bsymbolic \
	-L/usr/lib -L/usr/lib64

.PHONY: all efi image clean

all: efi

efi: $(EFI)

$(BUILD):
	mkdir -p $(BUILD)

$(LOADER_OBJ): boot/uefi/main.c kernel/boot-info.h | $(BUILD)
	$(CC) $(LOADER_CFLAGS) -c $< -o $@

$(KERNEL_OBJ): kernel/kernel.c kernel/boot-info.h kernel/mm.h kernel/scheduler.h kernel/usb.h kernel/net.h kernel/wifi.h kernel/storage.h kernel/ipc.h kernel/hot-shadow.h runtime/jx/jx-runtime.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(MM_OBJ): kernel/mm.c kernel/mm.h kernel/boot-info.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(SCHED_OBJ): kernel/scheduler.c kernel/scheduler.h kernel/security.h runtime/jx/jx-runtime.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(USB_OBJ): kernel/usb.c kernel/usb.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -Wno-unused-function -c $< -o $@

$(USB_HOT_OBJ): kernel/usb-hot.c kernel/usb-hot.h kernel/usb.h kernel/hot-shadow.h kernel/security.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(E1000_OBJ): kernel/e1000.c kernel/e1000.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(NET_OBJ): kernel/net.c kernel/net.h kernel/e1000.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(WIFI_OBJ): kernel/wifi.c kernel/wifi.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(WIFI_HOT_OBJ): kernel/wifi-hot.c kernel/wifi-hot.h kernel/wifi.h kernel/hot-shadow.h kernel/security.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(CLOCK_HOT_OBJ): kernel/clock-hot.c kernel/clock-hot.h kernel/hot-shadow.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(MEMORY_HOT_OBJ): kernel/memory-hot.c kernel/memory-hot.h kernel/hot-shadow.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(TASK_HOT_OBJ): kernel/task-hot.c kernel/task-hot.h kernel/scheduler.h kernel/hot-shadow.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(VFS_OBJ): kernel/vfs.c kernel/vfs.h kernel/storage.h kernel/hot-shadow.h kernel/security.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BOOK_HOT_OBJ): kernel/book-hot.c kernel/book-hot.h kernel/hot-shadow.h runtime/jx/jx-runtime.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(SECURITY_OBJ): kernel/security.c kernel/security.h kernel/hot-shadow.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(HOT_SHADOW_OBJ): kernel/hot-shadow.c kernel/hot-shadow.h kernel/usb-hot.h kernel/wifi-hot.h kernel/clock-hot.h kernel/memory-hot.h kernel/task-hot.h kernel/vfs.h kernel/book-hot.h kernel/security.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(STORAGE_OBJ): kernel/storage.c kernel/storage.h kernel/hot-shadow.h kernel/security.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(IPC_OBJ): kernel/ipc.c kernel/ipc.h kernel/hot-shadow.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(JX_RUNTIME_OBJ): runtime/jx/jx-runtime.c runtime/jx/jx-live-tail.c runtime/jx/jx-runtime.h | $(BUILD)
	cat runtime/jx/jx-runtime.c runtime/jx/jx-live-tail.c | \
		$(CC) $(KERNEL_CFLAGS) -Dosaura_jx_runtime_task=osaura_jx_runtime_task_legacy \
		-x c -c - -o $@

$(JX_LIVE_OBJ): runtime/jx/jx-live.c runtime/jx/jx-runtime.h kernel/security.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(ARCH_OBJ): kernel/x86_64.S | $(BUILD)
	$(CC) $(COMMON_FLAGS) -c $< -o $@

$(SO): $(LOADER_OBJ) $(KERNEL_OBJ) $(MM_OBJ) $(SCHED_OBJ) $(USB_OBJ) $(USB_HOT_OBJ) $(E1000_OBJ) $(NET_OBJ) $(WIFI_OBJ) $(WIFI_HOT_OBJ) $(CLOCK_HOT_OBJ) $(MEMORY_HOT_OBJ) $(TASK_HOT_OBJ) $(VFS_OBJ) $(BOOK_HOT_OBJ) $(SECURITY_OBJ) $(HOT_SHADOW_OBJ) $(STORAGE_OBJ) $(IPC_OBJ) $(JX_RUNTIME_OBJ) $(JX_LIVE_OBJ) $(ARCH_OBJ)
	$(LD) $(LDFLAGS) $(EFI_CRT) $(LOADER_OBJ) $(KERNEL_OBJ) $(MM_OBJ) $(SCHED_OBJ) $(USB_OBJ) $(USB_HOT_OBJ) $(E1000_OBJ) $(NET_OBJ) $(WIFI_OBJ) $(WIFI_HOT_OBJ) $(CLOCK_HOT_OBJ) $(MEMORY_HOT_OBJ) $(TASK_HOT_OBJ) $(VFS_OBJ) $(BOOK_HOT_OBJ) $(SECURITY_OBJ) $(HOT_SHADOW_OBJ) $(STORAGE_OBJ) $(IPC_OBJ) $(JX_RUNTIME_OBJ) $(JX_LIVE_OBJ) $(ARCH_OBJ) -o $@ -lefi -lgnuefi

$(EFI): $(SO)
	$(OBJCOPY) \
		-j .text -j .rodata -j .sdata -j .data -j .bss -j .dynamic -j .dynsym \
		-j .rel -j .rela -j .reloc \
		--target=efi-app-x86_64 $< $@

image: $(EFI)
	bash scripts/make-image.sh

clean:
	rm -rf $(BUILD)
