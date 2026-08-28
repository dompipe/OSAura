# TODO: Windows-hosted OSAura compatibility layer

## Goal

Create a Windows interface, analogous in purpose to WSL, that lets developers run OSAura/JX programs and exercise OSAura kernel-facing APIs without booting the full OSAura image.

The host must emulate OSAura semantics rather than merely wrap Windows command-line tools. Canonical JX source and compiled `.64B` Books should see the same logical kernel contracts they see when running on native OSAura.

## Working name

`OSAura Host for Windows` (`OHW`) until a final product name is chosen.

## Core architecture

```text
Windows process
    |
    +-- jx.exe / OSAura host launcher
    |
    +-- OSAura ABI compatibility layer
    |      |
    |      +-- 0x80-0xEF hot-shadow dispatcher
    |      +-- security subjects/capabilities
    |      +-- task/job model
    |      +-- VFS/storage facade
    |      +-- IPC/channel bus
    |      +-- clock/memory primitives
    |      +-- network/Wi-Fi/USB facades
    |
    +-- Windows backend adapters
           |
           +-- files/directories -> Win32/NT filesystem
           +-- sockets -> Winsock
           +-- clock -> QPC / Windows timers
           +-- processes/threads -> Windows threads or fibers where appropriate
           +-- terminal -> ConPTY / console
           +-- memory -> VirtualAlloc / mapped sections
           +-- devices -> explicitly virtualized or unavailable
```

## ABI rule

The Windows host must preserve the canonical OSAura hot-call ABI instead of inventing a second runtime model.

```text
1bbbssss                  -> one-byte hot OSAura operation
0fffffff xxxxxxxx         -> two-byte extended operation
```

The same bank/shadow numbers used by native OSAura should identify the same logical mechanisms in the Windows host. Backend implementation may differ, but JX code must not need source changes merely because execution moved from native OSAura to Windows-hosted OSAura.

## Required compatibility layers

### 1. JX / `.64B` execution

- Load and verify native OSAura `.64B` Books.
- Reuse ABI v4 one-byte hot calls.
- Reuse Bag, generation, prepared-call, hot-register, and reaction semantics.
- Preserve generation swap/checkpoint behavior.
- Keep canonical JX source independent of the host operating system.

### 2. Security subjects

- Preserve OSAura security subject IDs and capability masks.
- Kernel-host adapter acts as trusted subject `0`.
- JX runtime defaults to subject `1`.
- Never map Windows process ownership directly to OSAura administrator rights without an explicit admission rule.
- Preserve generation-based capability invalidation when prepared permission tokens are introduced.

### 3. VFS

Expose the OSAura VFS contract through Windows paths while keeping OSAura handles and capability checks above Win32 handles.

Initial mapping proposal:

```text
/host/c/...       -> C:\...
/host/d/...       -> D:\...
/home/...         -> configurable OSAura-host home directory
/tmp/...          -> isolated host temporary directory
```

The Windows backend should not leak raw HANDLE values into JX or OSAura APIs.

### 4. Storage

- Provide virtual block devices backed by Windows files.
- Allow creation of reproducible disk images for tests.
- Keep raw block-write capability separate from VFS-write capability.
- Native physical-drive access should be opt-in and privileged, never the default.

### 5. Tasks/jobs

Map the OSAura scheduler/task model to a Windows-host execution model while preserving OSAura task IDs, subjects, roles, foreground/background state, and task-control capability checks.

Early versions can use cooperative host tasks or Windows threads. The semantic ABI matters more than reproducing native interrupt scheduling exactly.

### 6. IPC/channel bus

- Reuse the JX channel-bus model.
- Host-local channels should avoid Windows networking when both endpoints are in one process.
- Optional cross-process transport can use named pipes or shared memory later.

### 7. Networking

Map OSAura socket/network operations to Winsock behind the OSAura network ABI. OSAura `NETWORK` capability must be checked before opening/transmitting through Windows sockets.

Raw Ethernet and adapter-control operations must remain unavailable unless a dedicated privileged backend is explicitly enabled.

### 8. Wi-Fi

Do not pretend Windows gives the same hardware-level control as native OSAura drivers.

Host mode should expose only operations the Windows WLAN APIs can safely provide. Credential-store access remains protected by the separate `WIFI_CREDENTIAL` capability. Native AX200 driver development remains an OSAura-native concern.

### 9. USB

Host USB should begin as capability-aware device enumeration/status with explicitly supported virtual devices. Direct arbitrary USB control transfer passthrough should not be assumed.

### 10. Terminal

Use ConPTY or the Windows console as the backend while preserving OSAura terminal/session semantics above it.

## CLI target

Potential interface:

```text
osaura.exe run program.64B
osaura.exe jx source.jx
osaura.exe shell
osaura.exe mount C:\work /host/work
osaura.exe image create dev.img 2G
osaura.exe image attach dev.img
osaura.exe status
```

`jx.exe` may also detect the host automatically and enter the compatibility layer when native OSAura is not present.

## Development phases

### Phase 1 — Runtime-only host

- Windows executable launches existing JX runtime.
- ABI v4 hot dispatcher compiled for Win64.
- Clock, memory, Book, Bag, reactions, security, IPC supported.
- No fake hardware.

### Phase 2 — Files and terminal

- VFS path namespace.
- Win32 file backend.
- virtual block images.
- terminal/ConPTY integration.

### Phase 3 — Tasks and networking

- subject-carrying task objects.
- foreground/background controls.
- Winsock network adapter.
- capability enforcement parity tests.

### Phase 4 — Device facade

- Windows WLAN integration.
- USB enumeration/selected passthrough.
- explicit unsupported-operation reporting for hardware behavior Windows cannot faithfully expose.

### Phase 5 — Developer integration

- `jx.exe --osaura-host` or equivalent.
- debugger/profiler hooks.
- VS Code integration.
- native-vs-host conformance suite.

## Conformance requirement

Every mechanism implemented in host mode should have paired tests:

```text
same logical call
    |
    +-- native OSAura backend
    +-- Windows host backend

observable semantic result must match
```

The host must never silently report success for an unimplemented native mechanism. Unsupported hardware behavior should return an explicit unsupported result.

## Performance objective

The Windows host should preserve the same hot-path principle as native OSAura:

```text
resolve Windows resource once
        -> bind OSAura handle/shadow
        -> repeat through numeric/prelinked path
```

Avoid repeated path parsing, string lookup, capability resolution, or Win32 handle discovery on prepared repeat paths.

## Architectural boundary

This is not "OSAura rewritten on Windows."

It is a host implementation of OSAura mechanisms so that the same JX/OAura execution model can run in two environments:

```text
                canonical JX / .64B
                       |
                 OSAura ABI
                  /        \
          native kernel   Windows host
```

Native OSAura remains the reference kernel. The Windows host is a compatibility/development environment and must stay subordinate to the canonical OSAura ABI.
