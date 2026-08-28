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
        -> compiler-defined Bag schema
        -> prelinked 1-byte / 2-byte prepared calls
        -> O(1) W:reg:slot:shadow hot routing
        -> prelinked reactions
        -> immutable generation roots
        -> JX channel bus / quiescent root switching
        -> applied BUS.TICK / BUS.COLLECT entrypoints
     -> IDLE task
     -> drivers / storage / networking (next layers)
     -> JX11 (later)
```

## Design laws

- OSAura owns hardware; hosted Windows/Linux adapters remain compatibility targets in `dompipe/jx`.
- Canonical JX remains human-readable truth; native/prepared forms accelerate execution.
- Native installation consumes compiled JX Books, not PHP source.
- A JX service is scheduled only after its `.64B` Book passes verification **and its compiler-authored runtime tables cross-link successfully**.
- Admission may parse names and validate hashes; the scheduled hot path must not repeat that work.
- Bags are the persistent/canonical state boundary; hot work stays in native shadows until checkpoint.
- Hot events use numeric register/slot/shadow routes and prelinked prepared bindings.
- Generation changes swap immutable roots at a quiescent boundary instead of rewriting live instructions.
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

The current Book carries seven runtime sections:

```text
BAG/schema.bin
CODE/applied-bus.bin
CODE/prepared.bin
HOT/calls.bin
HOT/reactions.bin
HOT/registers.bin
META/generations.bin
```

## Fast JX execution path

The new boundary deliberately separates admission work from repeated execution.

At admission OSAura:

1. verifies the complete `.64B` identity/content chain;
2. validates the compiler-defined Bag schema and records numeric field slots;
3. decodes compact prepared-call bytes once for every generation;
4. cross-links reaction IDs to prepared bindings;
5. builds an 8 x 8 x 8 hot route index for `W:register:slot:shadow`;
6. validates generation endpoints and creates immutable generation roots.

After admission the scheduled path is:

```text
W:reg:slot:shadow
      |
      v
route_index[reg][slot][shadow]
      |
      v
prelinked reaction
      |
      v
prelinked prepared binding
      |
      v
numeric native operation
```

There is no field-name lookup, string hashing, canonical parsing, object discovery or repeated prepared-byte decoding in that hot route.

The current compiled prepared stream proves all three compact forms used by this migration layer:

```text
0x80              promoted one-byte call
0x81              promoted one-byte call
0x00 0x02         sparse family/slot two-byte call
0xC0              one-byte microcall with selector r0
```

The compact bytes remain the portable representation; admission turns them into the smaller native execution bindings used by the scheduled runtime.

## Bags, reactions and generations

`BAG/schema.bin` now defines the live record layout. The Bag tracks heartbeat, bus activity, channel activity, prepared-call executions, hot dispatches, reaction state, generation swaps and active generation.

Hot Bag updates stay in the native shadow. `BUS.COLLECT` copies dirty hot state across the canonical checkpoint boundary and advances the Bag revision/checkpoint count.

`HOT/registers.bin` supplies numeric event routes. `HOT/reactions.bin` connects those routes to prepared calls. The current bootstrap proves two routes in generation 1 and two corresponding routes in generation 2.

`META/generations.bin` currently carries two immutable roots. OSAura starts on generation 1, executes its prepared/hot routes, then performs a quiescent **generation 1 -> 2** cutover by pausing the bus, checkpointing the Bag, switching the active high-bit program endpoint and generation root, queueing a message, and resuming delivery to generation 2 only.

That proves the intended update model:

```text
old root -> quiescent boundary -> new root
```

rather than reparsing canonical JX or rewriting live code.

## Applied bus

The applied bus section exposes the stable JX entrypoints:

```text
7f 00 01   BUS.TICK
7f 00 02   BUS.COLLECT
```

They are entrypoints, not a six-byte sequential source program. The scheduled runtime invokes them explicitly from the verified `CODE/applied-bus.bin` section.

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

`make image` builds the deterministic JX runtime Book, creates the FAT32 image, and installs both the EFI executable and Book.

GitHub boot CI runs the image under OVMF/QEMU and gates all of these together:

- VM ownership and IRQs
- physical allocator
- preemptive three-task scheduler
- `.64B` package integrity and admission
- compiler-defined Bag schema
- Bag checkpointing
- compact prepared-call execution
- O(1) hot-register dispatch
- reactions
- generation 1 -> 2 cutover
- channel queued delivery/program switching
- applied `BUS.TICK` / `BUS.COLLECT`
- terminal responsiveness while JX is being preempted

Writing an image directly to a device destroys the previous contents of that device. Verify the target device name before using a raw imaging tool.

## JX migration source

OSAura currently tracks the host-neutral runtime contract from `dompipe/jx` source head:

```text
3bef4af1b8c5bb1e8e04be85003b94c08f72ceed
```

See `docs/JX-MIGRATION.md` for the portability boundary, the exact admission/hot-path split, what is kernel-live, and the next migration layers.
