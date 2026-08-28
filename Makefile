BUILD := build
EFI := $(BUILD)/BOOTX64.EFI
LOADER_OBJ := $(BUILD)/loader.o
KERNEL_OBJ := $(BUILD)/kernel.o
MM_OBJ := $(BUILD)/mm.o
SCHED_OBJ := $(BUILD)/scheduler.o
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
# The current preemptive scheduler saves the complete integer context. Keep
# freestanding kernel/runtime C inside that ABI: no host CET entry markers and
# no implicit MMX/SSE/AVX state until OSAura has explicit extended-state save.
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

$(KERNEL_OBJ): kernel/kernel.c kernel/boot-info.h kernel/mm.h kernel/scheduler.h runtime/jx/jx-runtime.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(MM_OBJ): kernel/mm.c kernel/mm.h kernel/boot-info.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(SCHED_OBJ): kernel/scheduler.c kernel/scheduler.h runtime/jx/jx-runtime.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

# Keep the already-gated verifier/prelinker source intact. The live tail is
# concatenated into the same translation unit so it can reuse private verifier,
# Bag, channel, and root state without making those internals public ABI.
# Rename the old task entry while compiling the base source; the tail undefines
# the macro and supplies the scheduler-visible live task entry.
$(JX_RUNTIME_OBJ): runtime/jx/jx-runtime.c runtime/jx/jx-live-tail.c runtime/jx/jx-runtime.h | $(BUILD)
	cat runtime/jx/jx-runtime.c runtime/jx/jx-live-tail.c | \
		$(CC) $(KERNEL_CFLAGS) -Dosaura_jx_runtime_task=osaura_jx_runtime_task_legacy \
		-x c -c - -o $@

$(JX_LIVE_OBJ): runtime/jx/jx-live.c runtime/jx/jx-runtime.h | $(BUILD)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(ARCH_OBJ): kernel/x86_64.S | $(BUILD)
	$(CC) $(COMMON_FLAGS) -c $< -o $@

$(SO): $(LOADER_OBJ) $(KERNEL_OBJ) $(MM_OBJ) $(SCHED_OBJ) $(JX_RUNTIME_OBJ) $(JX_LIVE_OBJ) $(ARCH_OBJ)
	$(LD) $(LDFLAGS) $(EFI_CRT) $(LOADER_OBJ) $(KERNEL_OBJ) $(MM_OBJ) $(SCHED_OBJ) $(JX_RUNTIME_OBJ) $(JX_LIVE_OBJ) $(ARCH_OBJ) -o $@ -lefi -lgnuefi

$(EFI): $(SO)
	$(OBJCOPY) \
		-j .text -j .rodata -j .sdata -j .data -j .bss -j .dynamic -j .dynsym \
		-j .rel -j .rela -j .reloc \
		--target=efi-app-x86_64 $< $@

image: $(EFI)
	bash scripts/make-image.sh

clean:
	rm -rf $(BUILD)
