# JX -> OSAura migration

OSAura imports the OS-relevant JX runtime contract from `dompipe/jx` source head:

`3bef4af1b8c5bb1e8e04be85003b94c08f72ceed`

The migration tracks the host-neutral JX runtime contract while preserving the core JX rule: canonical source stays readable, but the admitted execution path is numeric, compact and prelinked.

## Running boundary

The native scheduled path is now:

```text
canonical JX / compiler
        |
        v
runtime.64B
  JX64/header.bin
  JX64/manifest.json
  BAG/schema.bin
  CODE/applied-bus.bin
  CODE/prepared.bin
  HOT/calls.bin
  HOT/registers.bin
  HOT/reactions.bin
  META/generations.bin
        |
        v
UEFI reads OSAURA/runtime.64B as EfiLoaderData
        |
        v
OSAura .64B verifier
  STORE/ZIP structure + CRC32
  JX64B001 + version + exact section set
  manifest SHA-256
  section byte counts + SHA-256
  canonical content SHA-256
        |
        v
admission linker
  Bag semantic names -> numeric slots
  compact prepared bytes -> native numeric bindings
  W register/slot/shadow -> O(1) route index
  reaction -> prepared binding
  generation -> endpoint + immutable hot root
        |
        v
scheduler admission
        |
        +--> SHELL
        +--> JX-RUNTIME
        `--> IDLE

JX-RUNTIME hot path
  event W:reg:slot:shadow
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
        |
        +--> Bag hot shadow
        +--> channel bus
        `--> explicit canonical checkpoint
```

The important admission law is executable: **the JX runtime task is not made runnable unless its boot-loaded `.64B` Book verifies and all compiler-authored runtime tables cross-link successfully.**

## The speed boundary

OSAura does not parse canonical names or rediscover objects in the scheduled hot path.

Expensive or defensive work happens once at Book admission:

- ZIP/STORE and CRC validation
- SHA-256 identity/content validation
- Bag field-name validation
- prepared-call decoding
- generation validation
- reaction cross-linking
- hot-register route construction
- endpoint validation

After admission, the live route is intentionally small:

```text
3 numeric coordinates
    -> byte route index
    -> prelinked prepared binding
    -> numeric native operation
```

The current route index is an 8 x 8 x 8 table. The coordinates therefore stay compatible with the three-bit hot-register model without string lookup, hashing, tree walking or dynamic object resolution.

This is the kernel form of the JX execution rule:

> Compile meaning once. Execute numbers repeatedly.

## `.64B` ownership

OSAura follows `jx.64B/1` rather than inventing an OS-only executable format. `scripts/make-jx-runtime-book.py` creates the deterministic bootstrap compiled Book and `scripts/make-image.sh` installs it as:

```text
OSAURA/runtime.64B
```

The UEFI loader reads that file before `ExitBootServices`. Its loader-owned memory is carried through the boot handoff and must fit inside OSAura's current 64-GiB direct physical map.

The current bootstrap Book contains seven compiler/runtime sections:

```text
BAG/schema.bin
CODE/applied-bus.bin
CODE/prepared.bin
HOT/calls.bin
HOT/reactions.bin
HOT/registers.bin
META/generations.bin
```

The package identity, manifest, every section digest and the canonical content digest are checked before any section is admitted.

## Compiler-defined Bag schema

`BAG/schema.bin` is now live rather than a placeholder. The compiler emits semantic field rows; OSAura validates those rows at admission and records the numeric slots in an admitted Bag layout.

The current bootstrap schema carries:

- heartbeat
- bus ticks
- bus collects
- channel messages
- channel deliveries
- channel switches
- last message type
- prepared-call executions
- hot dispatches
- reaction runs
- reaction value
- generation swaps
- active generation

The runtime code uses the admitted numeric layout after startup. It does not perform field-name lookup while scheduled.

Hot updates mark the record Bag dirty. `BUS.COLLECT` crosses the explicit canonical boundary: hot values are copied to the canonical checkpoint image, the Bag revision advances, the checkpoint counter advances, and execution returns to the hot shadow.

This preserves the JX rule:

> Be native while working. Become canonical at the boundary.

## Prepared-call tiers

`CODE/prepared.bin` and `HOT/calls.bin` now exercise all three compact prepared-call forms used by this migration step:

```text
0x80              promoted one-byte call
0x81              promoted one-byte call
0x00 0x02         sparse family/slot two-byte call
0xC0              one-byte microcall carrying selector r0
```

The compact bytes remain the portable compiled representation. OSAura decodes them **once at admission** for every generation and stores numeric `jx_prepared_binding` records containing only what the hot executor needs.

The scheduled reaction path therefore does not repeatedly decode `0x80`, `0xC0`, family numbers or call-table rows.

The bootstrap native operations prove:

- direct heartbeat increment
- reaction-run increment
- reaction-value addition from the selected hot-frame register

Prepared-call execution is separately gated in OVMF CI.

## Hot registers and reactions

`HOT/registers.bin` maps generation-scoped numeric routes of the form:

```text
W:register:slot:shadow -> reaction-id
```

`HOT/reactions.bin` maps each reaction to:

- prepared-code offset
- frame-register selector
- immediate bootstrap scalar
- flags

During admission, OSAura resolves the reaction and prepared offset and stores the final route in the generation root. The live dispatcher then uses:

```text
route_index[reg][slot][shadow]
```

for constant-time route selection.

The bootstrap proves two hot routes in each generation. Generation 1 uses a reaction scalar of `3`; generation 2 uses `5`, proving that the same numeric event coordinate can move to a new immutable reaction root after cutover without rewriting the event producer.

## Generations

`META/generations.bin` is now live. Each admitted generation has:

- generation number
- high-bit JX program endpoint ID
- prelinked prepared-call table
- prelinked reaction routes
- O(1) route index

The bootstrap Book carries generation 1 and generation 2.

OSAura begins on generation 1, executes its prepared/hot routes, then performs a quiescent generation 1 -> 2 cutover:

1. require an empty channel queue;
2. pause the channel bus;
3. checkpoint dirty Bag state;
4. switch the active program endpoint;
5. swap the active immutable generation root;
6. record previous/active generation state;
7. queue a boot message while paused;
8. resume and prove delivery to generation 2 only.

No active instruction stream is rewritten. The cutover changes roots.

That is the intended fast update model:

```text
old immutable root --> quiescent boundary --> new immutable root
```

rather than reparsing or relinking canonical JX during execution.

## Applied runtime page

OSAura consumes JX `AppliedBytecode::runtimeBusPage()` as an entry table:

```text
offset 0: 7f 00 01   BUS.TICK
offset 3: 7f 00 02   BUS.COLLECT
```

These are two stable entrypoints, not a six-byte sequential source program. The scheduled service explicitly invokes the two verified entrypoints from `CODE/applied-bus.bin`.

## Channels

OSAura implements the current host-neutral JX channel-bus semantics in the scheduled runtime:

- bus version 1
- 64 endpoint slots
- 64 channel bindings per endpoint
- 64-message paused queue
- IN / OUT / INOUT binding law
- source-output validation
- delivery only to input-bound endpoints
- high-bit program endpoint IDs
- only the active program generation receives live program traffic
- pause -> queue -> root/endpoint switch -> resume/drain

The generation cutover now uses this same channel mechanism instead of an unrelated bootstrap switch.

## What OVMF CI proves

The boot gate now requires one real OVMF/QEMU run to prove all of the following together:

- UEFI boot and `ExitBootServices`
- OSAura-owned CR3/page tables
- PIT IRQ0 and PS/2 IRQ1
- physical page allocation
- three scheduled tasks
- nonzero `JX-RUNTIME` and `IDLE` scheduler ticks
- deterministic `.64B` copied intact into the FAT image
- `.64B` identity/content verification
- compiler-defined Bag schema linked
- Bag hot state and canonical checkpoint
- all prepared-call tiers admitted and executed
- hot-register O(1) routes executed
- reactions executed
- generation 1 -> 2 cutover
- channel switch/queued delivery
- applied `BUS.TICK`
- applied `BUS.COLLECT`
- shell HELP/VM/TASKS/ALLOC responsiveness while JX is preempted

A build-only success is not sufficient for this layer.

## Included current JX subsystems

The broader migration target still includes:

- canonical Bag/container runtime and Bag listeners/patching
- vector/stack/queue/deque/map/set Bag disciplines
- channels and channel-root continuity
- task manager/task control abstractions
- hot generation, rollback, execution branch/shadow state
- compact ASM/prepared-call runtime
- applied bytecode ABI
- idle bitmap/codebus/domain runtime
- CORE / WINDOW / SECURITY logical domains
- multiplex-bus clock policy and adaptive logical scheduling state
- JX Security scanner
- whole-object/file hash matching and security result references
- `.64B` compiled-Book/native section model

The `.64B` verifier, compiler Bag schema, prepared calls, hot-register routes, reactions, two generation roots, generation cutover, channel bus, applied bus page and scheduler service are kernel-live now. The remaining items stay migration targets until they receive native implementations and boot gates.

## Adapt rather than copy as kernel code

Windows and Linux host adapters are not OSAura kernel dependencies. Their behavior remains compatibility/reference material where useful, while OSAura gets native implementations for:

- wake/interrupt delivery
- process/task ownership
- memory
- storage
- console/input
- networking
- device drivers

The Windows named-event wake backend and Linux futex backend remain proofs of the same JX applied ABI; OSAura implements that ABI directly on its scheduler and interrupt primitives.

## Rule

> Preserve JX semantics and canonical source contracts; replace hosted OS mechanisms with OSAura-native mechanisms, and move repeated resolution out of the hot path.

JX remains the language/compiler/runtime project. OSAura owns boot, kernel, scheduling, drivers, terminal, hardware authority, the boot image, and later JX11 as a graphical service.
