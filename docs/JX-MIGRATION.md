# JX -> OSAura migration

OSAura imports the OS-relevant JX runtime contract from `dompipe/jx` source head:

`3bef4af1b8c5bb1e8e04be85003b94c08f72ceed`

The migration tracks the current host-neutral JX runtime rather than copying an older hosted snapshot.

## Running boundary

The first JX execution path is now native and scheduled:

```text
canonical JX / compiler
        |
        v
runtime.64B
  JX64/header.bin
  JX64/manifest.json
  BAG/schema.bin
  CODE/applied-bus.bin
        |
        v
UEFI reads OSAURA/runtime.64B as EfiLoaderData
        |
        v
OSAura .64B verifier
  STORE/ZIP structure + CRC32
  JX64B001 + version + section count
  manifest SHA-256
  section byte counts + SHA-256
  canonical content SHA-256
        |
        v
scheduler admission
        |
        +--> SHELL
        +--> JX-RUNTIME
        `--> IDLE
                 
JX-RUNTIME
  -> record Bag hot shadow
  -> explicit canonical Bag checkpoint
  -> channel endpoint/binding bus
  -> paused message queue
  -> active-program generation switch
  -> applied BUS.TICK entrypoint
  `-> applied BUS.COLLECT entrypoint
```

The important admission law is now executable rather than documentary: **the JX runtime task is not made runnable unless its boot-loaded `.64B` Book verifies.**

## `.64B` ownership

OSAura follows `jx.64B/1` rather than inventing an OS-only executable format. `scripts/make-jx-runtime-book.py` creates the bootstrap compiled Book deterministically and `scripts/make-image.sh` installs it as:

```text
OSAURA/runtime.64B
```

The UEFI loader reads that file before `ExitBootServices`. Its loader-owned memory is carried through the boot handoff and required to fit inside OSAura's current 64-GiB direct physical map.

The runtime recognizes package bytes, not the filename extension. The current kernel verifier accepts the portable section-name subset used by native JX runtime sections and verifies the complete bootstrap package identity/content chain before exposing section pointers.

## Applied runtime page

OSAura consumes JX `AppliedBytecode::runtimeBusPage()` exactly as an entry table:

```text
offset 0: 7f 00 01   BUS.TICK
offset 3: 7f 00 02   BUS.COLLECT
```

These are two stable entrypoints, **not** a request to interpret the six bytes as a sequential source program. The scheduled service explicitly invokes the tick and collect entrypoints from the verified `CODE/applied-bus.bin` section.

## Bags

The bootstrap runtime now has a native record-Bag hot shadow. Current dense slots track:

- heartbeat
- bus ticks
- bus collects
- channel messages
- channel deliveries
- active-program switches
- last message type

Hot updates mark the Bag dirty. `BUS.COLLECT` crosses the explicit canonical boundary: the dense hot slots are copied into the canonical checkpoint image, the Bag revision advances once, and the checkpoint counter advances once.

This follows the JX rule:

> Be native while working. Become canonical at the boundary.

The next Bag expansion is to load compiler-defined field/discipline metadata from `BAG/schema.bin` instead of using only the bootstrap record layout, then add the vector/stack/queue/deque/map/set native disciplines behind the same Bag boundary.

## Channels

OSAura now implements the host-neutral JX channel-bus semantics in the scheduled runtime:

- bus version 1
- 64 endpoint slots
- 64 channel bindings per endpoint
- 64-message paused queue
- IN / OUT / INOUT binding law
- source-output validation
- delivery only to input-bound endpoints
- high-bit program endpoint IDs
- only the active program generation receives live program traffic
- pause -> queue -> switch active program -> resume/drain

The runtime performs a real generation-switch admission test before announcing the channel bus active. Normal applied ticks then publish through the same bus while the scheduler continues preempting the service.

## Included current JX subsystems

The migration target includes:

- canonical Bag/container runtime and Bag listeners/patching
- channels and channel-root continuity
- task manager/task control abstractions
- hot generation, rollback, execution branch/shadow state
- compact ASM/prepared-call runtime pieces needed by native JX execution
- applied bytecode ABI, including the JX system escape/bus operations
- idle bitmap/codebus/domain runtime
- CORE / WINDOW / SECURITY logical domains
- multiplex-bus clock policy and adaptive logical scheduling state
- JX Security scanner
- whole-object/file hash matching and security result references
- `.64B` compiled-Book format and native runtime section model

Not every item above is kernel-live yet. The `.64B` loader, applied bus page, bootstrap Bag, channel bus, scheduler service, VM and interrupt path are live now; the remaining items stay migration targets until their OSAura-native implementations are admitted and gated.

## Adapt rather than copy as kernel code

Windows and Linux host adapters are not OSAura kernel dependencies. Their behavior remains compatibility/reference material where useful, while OSAura gets native implementations for:

- wake/interrupt delivery
- process/task ownership
- memory
- storage
- console/input
- networking
- device drivers

The Windows named-event wake backend and Linux futex backend therefore remain proofs of the same JX applied ABI; OSAura implements that ABI directly on its own scheduler/interrupt primitives.

## Rule

> Preserve JX semantics and canonical source contracts; replace hosted OS mechanisms with OSAura-native mechanisms.

JX remains the language/compiler/runtime project. OSAura owns boot, kernel, scheduling, drivers, terminal, hardware authority, the boot image, and later JX11 as a graphical service.
