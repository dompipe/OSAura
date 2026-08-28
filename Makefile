BUILD := build
EFI := $(BUILD)/BOOTX64.EFI
OBJ := $(BUILD)/main.o
SO := $(BUILD)/bootx64.so

CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy

EFI_INC := /usr/include/efi
EFI_ARCH_INC := /usr/include/efi/x86_64
EFI_LDS := /usr/lib/elf_x86_64_efi.lds
EFI_CRT := /usr/lib/crt0-efi-x86_64.o

CFLAGS := -I$(EFI_INC) -I$(EFI_ARCH_INC) -I$(EFI_INC)/protocol \
	-fpic -ffreestanding -fno-stack-protector -fno-stack-check -fshort-wchar \
	-mno-red-zone -maccumulate-outgoing-args -Wall -Wextra -Werror -O2

LDFLAGS := -nostdlib -znocombreloc -T $(EFI_LDS) -shared -Bsymbolic \
	-L/usr/lib -L/usr/lib64

.PHONY: all efi image clean

all: efi

efi: $(EFI)

$(BUILD):
	mkdir -p $(BUILD)

$(OBJ): boot/uefi/main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(SO): $(OBJ)
	$(LD) $(LDFLAGS) $(EFI_CRT) $(OBJ) -o $@ -lefi -lgnuefi

$(EFI): $(SO)
	$(OBJCOPY) \
		-j .text -j .sdata -j .data -j .dynamic -j .dynsym \
		-j .rel -j .rela -j .reloc \
		--target=efi-app-x86_64 $< $@

image: $(EFI)
	bash scripts/make-image.sh

clean:
	rm -rf $(BUILD)
