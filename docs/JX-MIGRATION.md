# JX -> OSAura migration

OSAura imports the OS-relevant JX runtime from `dompipe/jx` source head:

`414cbccebe4e55daea8fe5778e0b134a785a022f`

The migration is intentionally based on the newest runtime state, not an older snapshot.

## Included current JX subsystems

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
- MD5, SHA-1 and SHA-256 whole-object hash matching
- ClamAV/phpMussel-compatible whole-file hash importer
- compact 1-2 byte security result references
- `.64B` executable/package format support and native runtime section model
- canonical security/Bags/container documentation needed to preserve semantics

## Adapt rather than copy as kernel code

Windows and Linux host adapters are not OSAura kernel dependencies. Their behavior is retained as compatibility/reference material where useful, while OSAura gets native implementations for:

- wake/interrupt delivery
- process/task ownership
- memory
- storage
- console/input
- networking
- device drivers

The Windows named-event wake backend and Linux futex backend therefore remain proofs of the same JX applied ABI; OSAura will implement that ABI directly on its own scheduler/interrupt primitives.

## Rule

> Preserve JX semantics and canonical source contracts; replace hosted OS mechanisms with OSAura-native mechanisms.

JX remains the language/runtime project. OSAura owns boot, kernel, drivers, terminal, hardware authority, USB image generation, and later JX11 as a graphical service.
