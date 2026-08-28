# OSAura / JX Hot-Call Kernel Bridge

## Purpose

This document defines how OSAura should host the JX hot-call ABI without turning the kernel into a JX parser or weakening JX canonicality.

It exists so later kernel, JX runtime, driver, scheduler, graphics, networking, storage, and JX11 releases can add hot paths without repeatedly redesigning the boundary.

The shared physical rule is:

```text
1xxxxxxx            = one-byte prepared hot call
0xxxxxxx xxxxxxxx   = two-byte extended call
```

For a one-byte hot call, the low three bits are always the shadow selector:

```text
shadow = opcode & 0x07
```

The four remaining payload bits select one of sixteen prepared banks:

```text
bank = (opcode >> 3) & 0x0F
```

Therefore the physical hot-call space is:

```text
16 banks x 8 shadows = 128 one-byte prepared entries
```

OSAura's existing eight-shadow discipline is intentionally compatible with this shape.

---

## 1. Boundary rule

JX owns:

- canonical JX meaning
- source names
- Bags
- executable `.64B` Books
- compiler lowering
- generation admission
- prepared call selection
- hot-register/reaction routing
- generation cutover and rollback

OSAura owns:

- kernel execution
- scheduler/process primitives
- memory management
- terminal/input
- storage
- PCI/USB/device access
- Ethernet/Wi-Fi/network stack
- TLS/HTTPS/SSH recovery services
- graphics/display primitives
- other privileged machine services

The hot boundary must not look like this:

```text
JX byte
  -> string name
  -> kernel command parser
  -> permission lookup
  -> service lookup
  -> driver call
```

It should look like this:

```text
canonical JX
   -> verify/authorize/prelink once
   -> prepared numeric kernel binding
   -> one-byte or two-byte selector
   -> direct prelinked service shadow
```

The kernel never needs to know the canonical spelling during repeated execution.

---

## 2. Existing OSAura shadow law

OSAura currently defines a kernel-wide eight-shadow table in `kernel/hot-shadow.h`.

The stable idea is:

```text
subsystem
   +-- shadow 0
   +-- shadow 1
   +-- shadow 2
   +-- shadow 3
   +-- shadow 4
   +-- shadow 5
   +-- shadow 6
   +-- shadow 7
```

Each shadow is already a prelinked function pointer plus context.

That design should remain the basic kernel primitive.

The JX ABI adds a bank above it:

```text
JX hot opcode
   |
   +-- bank 0  -> OSAura shadow table A
   +-- bank 1  -> OSAura shadow table B
   +-- ...
   +-- bank 15 -> OSAura shadow table P
```

Not every bank must represent a kernel subsystem. JX-native operations may occupy some banks, and the generation's prepared mapping decides what is local JX versus kernel-backed.

---

## 3. Decoder invariant

Any OSAura-side decoder for the shared executable ABI must determine width from one bit only:

```c
uint8_t opcode = code[0];

if (opcode & 0x80u) {
    /* one-byte prepared call */
} else {
    /* require one second byte */
}
```

This rule is deliberately simpler than JX ASM-call ABI v3.

Under the new ABI, `0xC0..0xFF` cannot begin a multi-byte microcall. They are ordinary one-byte prepared entries.

This means speculative instruction fetch is safer and instruction walking is trivial:

```text
MSB = 1 -> advance 1
MSB = 0 -> advance 2
```

No range table is needed to discover call width.

---

## 4. Kernel service preparation

A kernel call becomes hot-reachable only after the cold path has resolved all of the information that can be resolved once.

Preparation should resolve:

- service identity
- target function pointer
- context pointer
- ABI/frame shape
- calling capability
- owning process/program/generation
- resource limits
- device/interface selection where stable
- Bag/register/frame locations
- any immutable flags

Then the hot entry can be physically small:

```c
fn(context, request)
```

or, for a more specialized JX bridge, a prepared native operation that receives only a frame pointer.

A hot call must not trigger canonical service-name search.

---

## 5. Capability and security model

Prelinking does not remove security checks; it moves invariant checks out of the loop.

The sequence should be:

```text
request binding
   |
   v
validate executable Book
   |
   v
resolve requested kernel capability
   |
   v
authorize generation/program
   |
   v
bind prepared service shadow
   |
   v
publish candidate generation
   |
   v
hot execution may use numeric entry
```

If authorization changes dynamically, use a revocable generation/root/capability object rather than re-parsing names on every call.

Never permit untrusted `.64B` bytes to install arbitrary kernel pointers.

---

## 6. Candidate-generation rule

OSAura and JX already use the correct live-replacement model:

```text
running generation N
        |
        | build beside it
        v
candidate N+1
        |
        +-- verify
        +-- prelink
        +-- bind Bags
        +-- bind hot calls
        +-- bind kernel shadows
        +-- validate continuity
        |
        v
quiescent boundary
        |
        v
atomic root swap
```

Never rewrite active hot tables slot-by-slot while executable code may be using them.

The required maintenance law is:

> Patch beside, validate completely, then swap at a safe boundary.

---

## 7. Which kernel operations should become hot

The kernel should not make every service hot merely because the mechanism exists.

Hot entries are scarce and should be earned by measured use.

Good candidates are repeated, stable, cheaply parameterized operations such as:

- scheduler yield/wake primitives
- channel enqueue/dequeue
- timer reads
- prepared file-buffer operations
- prepared socket send/receive operations
- prepared graphics submission
- JX11 event/window primitives
- memory copy/fill kernels
- Bag-backed kernel exchange operations
- device queue submit/poll where capability and device are already fixed

Poor candidates are infrequent administrative operations with substantial cold-path work, such as:

- enumerating PCI devices
- parsing a URL
- scanning Wi-Fi SSIDs
- certificate-chain validation
- mounting a filesystem
- installing a package
- resolving a new hostname
- interactive shell command parsing

Those may *use* hot primitives internally, but their top-level operation does not need scarce one-byte space.

---

## 8. Driver hot paths

A future driver may expose an eight-shadow table internally without making the entire driver public ABI.

For example:

```text
network transmit bank
  shadow 0 -> send already-prepared Ethernet frame
  shadow 1 -> send prepared IPv4 packet
  shadow 2 -> send prepared UDP payload
  shadow 3 -> send prepared TCP segment
  shadow 4 -> queue prepared socket bytes
  shadow 5 -> poll receive queue
  shadow 6 -> reserved/learned hot operation
  shadow 7 -> reserved/learned hot operation
```

The exact assignments should usually be generation/prelink decisions, not permanent semantic promises.

The same applies to storage, graphics, input, audio, and other subsystems.

---

## 9. Wi-Fi and networking implication

The Wi-Fi boot menu, SSID scanning, WPA authentication, DHCP, DNS, TLS, and SSH are mostly cold/control-plane work.

They should not be forced through one-byte hot slots merely because networking is important.

Once a connection/session is established, repeated data movement becomes a better candidate:

```text
cold/control plane:
scan -> choose -> authenticate -> DHCP -> DNS -> TLS/SSH setup

hot/data plane:
queue bytes -> transmit -> receive -> acknowledge -> copy buffers
```

This distinction will matter as OSAura's network stack grows.

---

## 10. OSAura implementation direction

The existing `osaura_shadow_table` can remain the local eight-entry primitive.

A JX-facing prepared bridge can add a lightweight bank layer, conceptually:

```c
#define OSAURA_HOT_BANK_COUNT 16u
#define OSAURA_HOT_SHADOW_COUNT 8u

typedef struct {
    osaura_shadow_table bank[OSAURA_HOT_BANK_COUNT];
} osaura_hot_call_root;
```

Then a one-byte operation becomes:

```c
uint8_t index = opcode & 0x7fu;
uint8_t bank = index >> 3;
uint8_t shadow = index & 0x07u;
return osaura_shadow_dispatch(&root->bank[bank], shadow, request);
```

A flat 128-entry table may later benchmark faster and is acceptable. The logical `16 x 8` interpretation should remain stable even if the physical layout changes.

Do not add this layer to every subsystem independently. Prefer one reusable primitive and generation-scoped roots.

---

## 11. Extended calls

The two-byte form is not a slow-path mistake. It is the stable overflow space.

```text
0fffffff ssssssss
```

where:

```text
family = first_byte & 0x7F
slot   = second_byte
```

It provides up to 128 families x 256 slots without making the hot space semantically crowded.

A newly added service should normally start extended. Measurements can later justify promotion into a one-byte prepared slot.

This gives future releases a predictable progression:

```text
new canonical feature
   -> extended numeric binding
   -> measure
   -> stable hot candidate
   -> next-generation one-byte promotion
```

That is the preferred way to get momentum without repeatedly changing the executable format.

---

## 12. Migration from JX ASM-call v3

JX v3 distinguished:

```text
0x00..0x7F + slot = extended
0x80..0xBF        = promoted one-byte
0xC0..0xFF ...    = fused microcall
```

OSAura must not perpetuate that three-tier width rule.

For the shared v4 contract:

1. Every `0x80..0xFF` opcode becomes exactly one byte.
2. Existing fully prepared microcalls should be assigned ordinary hot bank/shadow entries.
3. Microcalls needing additional dynamic selector state must move that state into the frame/register/Bag or use the two-byte extended form.
4. The JX compiler/runtime and OSAura decoder must switch in the same release train.
5. ABI version checks must prevent accidental v3/v4 reinterpretation.

---

## 13. Cross-repository compatibility test

The most important new regression test is not merely a unit test in either repository. It is a shared fixture test.

JX should emit a tiny known executable fixture containing boundary opcodes such as:

```text
80
87
88
BF
C0
F8
FF
00 01
7F FF
```

OSAura should decode the same fixture and assert the same widths and target indexes.

Required width expectations:

```text
80    -> 1
87    -> 1
88    -> 1
BF    -> 1
C0    -> 1
F8    -> 1
FF    -> 1
00 01 -> 2
7F FF -> 2
```

This test should remain in CI permanently.

---

## 14. Performance tests

Kernel-side benchmarking should isolate:

- direct function call baseline
- `osaura_shadow_dispatch()`
- one-byte bank/shadow dispatch
- two-byte extended dispatch
- JX route-index + prepared call
- JX -> OSAura prepared service transition

Useful measurements include:

- cycles/call
- ns/call in hosted tests
- branch misses where available
- instruction count
- table footprint per generation
- hot versus extended call ratio in realistic workloads

Do not optimize only for synthetic single-operation loops. Preserve mixed-workload tests so one improvement does not regress the real scheduler/JX/driver interaction.

---

## 15. Release checklist

### Shared ABI

- [ ] MSB 1 always means exactly one byte.
- [ ] MSB 0 always means exactly two bytes.
- [ ] Low three hot bits still select one of eight shadows.
- [ ] Bank extraction still yields `0..15`.
- [ ] OSAura and JX use identical fixture expectations.

### Kernel hot path

- [ ] No canonical JX string lookup in the repeated path.
- [ ] No shell command parsing in the repeated path.
- [ ] No dynamic device-name lookup if it could have been prebound.
- [ ] No repeated policy lookup if a safe generation-scoped capability can represent it.
- [ ] No active-generation table mutation.

### Security

- [ ] All prepared kernel functions came from trusted kernel-owned tables.
- [ ] The requesting JX generation was authorized before publication.
- [ ] Frame/Bag/register bounds were validated.
- [ ] Revocation has a generation/root strategy.

### Performance

- [ ] One-byte and extended dispatch benchmarks run.
- [ ] Cross-boundary JX -> kernel benchmark runs.
- [ ] Results are compared with the previous release.
- [ ] A new hot feature has measured evidence for occupying a one-byte slot.

### Live update

- [ ] Candidate bindings are constructed off the active path.
- [ ] Candidate validation succeeds before swap.
- [ ] Failure leaves the active root untouched.
- [ ] Successful cutover is atomic at a safe boundary.

---

## 16. How future releases should add momentum

When adding a new subsystem or feature, use this progression:

```text
1. Build the correct canonical/control-plane feature.
2. Give it a stable numeric extended binding.
3. Measure realistic use.
4. Identify repeated invariant work.
5. Move that work into generation admission/prelink.
6. Promote only the resulting stable operation into one-byte space.
7. Re-run shared ABI, generation, and performance gates.
```

This prevents two common problems:

- prematurely spending hot-call space on features that are not actually hot;
- leaving obvious repeated work in parsers/lookups because the feature already 'works.'

The architecture should therefore get faster as releases mature without making the language or kernel API less readable.

---

## 17. The architectural sentence to preserve

Future maintainers and AI-assisted development should be able to recover the design from this sentence:

> **Canonical code is resolved once into generation-scoped prepared bindings; the repeated path chooses those bindings numerically, with one-byte bank/shadow calls for the hottest operations and two-byte family/slot calls for everything else.**

That is the bridge between readable JX, the `.64B` executable, and OSAura's close-to-metal runtime.
