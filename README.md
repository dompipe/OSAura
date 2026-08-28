# OSAura

OSAura is a terminal-first x86-64 UEFI operating system built to host the JX runtime directly on hardware and later run JX11 as its graphical environment.

## Architecture

```text
UEFI
  -> loads OSAURA/runtime.64B
  -> OSAura kernel
     -> owned page tables / interrupts / PIT scheduler
     -> SHELL task
     -> JX-RUNTIME task
        -> verified .64B compiled Book
        -> Bag hot state + canonical checkpoints
        -> JX channel bus / active-program switching
        -> applied BUS.TICK / BUS.COLLECT entrypoints
     -> IDLE task
     -> drivers / storage / networking (next layers)
     -> JX11 (later)
```

## Design laws

- OSAura owns hardware; hosted Windows/Linux adapters remain compatibility targets in `dompipe/jx`.
- Canonical JX remains human-readable truth; native/prepared forms accelerate execution.
- Native installation consumes compiled JX Books, not PHP source.
- A JX service is scheduled only after its `.64B` Book passes the OSAura admission verifier.
- Bags are the persistent/canonical state boundary; hot work stays in native shadows until checkpoint.
- The JX channel/multiplex bus is runtime policy and communication, not the kernel scheduler clock.
- The kernel provides mechanism. JX provides higher-level runtime semantics.
- Security scanning is an admission boundary, not an authorization substitute.
- The boot artifact remains one UEFI image usable in QEMU and on removable media.

## Current boot milestone

OSAura now boots through GNU-EFI, exits firmware boot services, owns its x86-64 page tables, GDT/IDT, PIC, PIT timer, PS/2 keyboard path, physical-page allocator and preemptive three-task scheduler.

The FAT32 boot image contains:

```text
EFI/BOOT/BOOTX64.EFI
OSAURA/osaura.cfg
OSAURA/runtime.64B
```

`runtime.64B` is a deterministic `jx.64B/1` compiled Book. UEFI reads it into loader-owned memory before `ExitBootServices`; OSAura verifies its ZIP/STORE structure, CRCs, JX64 identity header, manifest SHA-256, section SHA-256 values and canonical content SHA-256 before the scheduler admits `JX-RUNTIME`.

The scheduled runtime currently loads:

```text
BAG/schema.bin
CODE/applied-bus.bin
```

The applied bus section exposes the stable JX entrypoints:

```text
7f 00 01   BUS.TICK
7f 00 02   BUS.COLLECT
```

The runtime drives those entrypoints under timer preemption, maintains a record Bag hot shadow, checkpoints it at the collect boundary, publishes through the JX channel bus, and exercises paused queueing plus active-program generation switching before announcing itself active.

## Terminal

The current kernel terminal includes:

```text
help
about
mem
vm
tasks
ticks
alloc
clear
halt
```

`tasks` exposes the live scheduler counters for `SHELL`, `JX-RUNTIME`, and `IDLE`.

## Build

On a Debian/Ubuntu development host:

```bash
sudo apt-get install gnu-efi mtools dosfstools python3
make efi
make image
```

Outputs:

```text
build/BOOTX64.EFI
build/runtime.64B
build/osaura.img
```

`make image` builds the deterministic JX runtime Book, creates the FAT32 image, and installs both the EFI executable and Book. GitHub boot CI then runs the image under OVMF/QEMU and gates the VM, IRQs, allocator, scheduler, `.64B` verification, Bag checkpoint, channel bus, active-program switch, and applied JX execution together.

Writing an image directly to a device destroys the previous contents of that device. Verify the target device name before using a raw imaging tool.

## JX migration source

OSAura currently tracks the host-neutral runtime contract from `dompipe/jx` source head:

```text
3bef4af1b8c5bb1e8e04be85003b94c08f72ceed
```

See `docs/JX-MIGRATION.md` for the portability boundary, what is already kernel-live, and the next migration layers.
