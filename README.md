# OSAura

OSAura is a terminal-first x86-64 UEFI operating system built to host the JX runtime directly on hardware and later run JX11 as its graphical environment.

## Architecture

```text
UEFI
  -> OSAura kernel
     -> memory / interrupts / scheduler / syscalls
     -> drivers / storage / networking
     -> JX runtime
        -> Bags / channels / task manager
        -> SECURITY bus / signature scanner
        -> applied bytecodes / .64B loader
        -> osaura> terminal
        -> JX11 (later)
```

## Design laws

- OSAura owns hardware; hosted Windows/Linux adapters remain compatibility targets in `dompipe/jx`.
- Canonical JX remains human-readable truth; native/prepared forms accelerate execution.
- The kernel provides mechanism. JX provides higher-level policy and runtime semantics.
- Security scanning is an admission boundary, not an authorization substitute.
- The runtime multiplex bus is not the kernel scheduler clock.
- The first deliverable is one UEFI image usable in QEMU and on a USB flash drive.

## First boot milestone

The current bootstrap is an x86-64 GNU-EFI application installed at the standard removable-media fallback path:

```text
EFI/BOOT/BOOTX64.EFI
```

It clears the firmware console, displays the OSAura banner, queries the UEFI memory-map requirements and starts an interactive terminal:

```text
OSAura 0.1-dev
x86_64 UEFI terminal

osaura>
```

Current bootstrap commands are `help`, `about`, `mem`, `clear`, and `reboot`.

### Build

On a Debian/Ubuntu development host:

```bash
sudo apt-get install gnu-efi mtools dosfstools
make efi
make image
```

Outputs:

```text
build/BOOTX64.EFI
build/osaura.img
```

`osaura.img` is a FAT32 UEFI boot image containing `EFI/BOOT/BOOTX64.EFI` and `OSAURA/osaura.cfg`. The same image is intended for emulator testing and writing to removable USB media.

Writing an image directly to a device destroys the previous contents of that device. Verify the target device name before using a raw imaging tool.

## Migration source

The initial runtime core is being migrated from `dompipe/jx` at source head `414cbccebe4e55daea8fe5778e0b134a785a022f`.

See `docs/JX-MIGRATION.md` for the portability boundary and migration plan.
