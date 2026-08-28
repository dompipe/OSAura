BUILD := build
EFI := $(BUILD)/BOOTX64.EFI
LOADER_OBJ := $(BUILD)/loader.o
KERNEL_OBJ := $(BUILD)/kernel.o
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
KERNEL_CFLAGS := $(COMMON_FLAGS) -Ikernel

LDFLAGS := -nostdlib -znocombreloc -T $(EFI_LDS) -shared -Bsymbolic \
	-L/usr/lib -L/usr/lib64

.PHONY: all efi image clean

all: efi

efi: $(EFI)

$(BUILD):
	mkdir -p $(BUILD)

$(LOADER_OBJ): boot/uefi/main.c kernel/boot-info.h | $(BUILD)
	$(CC) $(LOADER_CFLAGS) -c $< -o $@

$(KERNEL_OBJ): kernel/kernel.c kernel/boot-info.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(ARCH_OBJ): kernel/x86_64.S | $(BUILD)
	$(CC) $(COMMON_FLAGS) -c $< -o $@

$(SO): $(LOADER_OBJ) $(KERNEL_OBJ) $(ARCH_OBJ)
	$(LD) $(LDFLAGS) $(EFI_CRT) $(LOADER_OBJ) $(KERNEL_OBJ) $(ARCH_OBJ) -o $@ -lefi -lgnuefi

$(EFI): $(SO)
	$(OBJCOPY) \
		-j .text -j .rodata -j .sdata -j .data -j .bss -j .dynamic -j .dynsym \
		-j .rel -j .rela -j .reloc \
		--target=efi-app-x86_64 $< $@

image: $(EFI)
	bash scripts/make-image.sh

clean:
	rm -rf $(BUILD)
