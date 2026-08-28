# TODO: Windows command-line OSAura emulator

## Goal

Create a real OSAura emulator that runs inside a normal Windows command-line window. The intended experience is much closer to opening `cmd.exe`, PowerShell, or Windows Terminal and entering OSAura than to running a hidden compatibility service.

The user launches:

```text
C:\> osaura.exe

OSAURA EMULATOR FOR WINDOWS
OSAURA KERNEL ABI: HOSTED
JX HOT ABI V4: ACTIVE
SECURITY POLICY: ACTIVE
VFS: WINDOWS BACKEND

OSAURA> 
```

From that point forward the window behaves like an OSAura terminal. Commands, JX programs, `.64B` Books, Bags, task state, VFS handles, capabilities, and hot-shadow calls use OSAura semantics while Windows supplies the underlying host resources.

## Product model

This is a **console-hosted OSAura emulator**.

```text
Windows Terminal / cmd.exe / PowerShell
                |
            osaura.exe
                |
        OSAura emulated kernel
                |
     +----------+----------+
     |          |          |
  JX/.64B      VFS       Tasks
     |          |          |
   Bags      Win32 FS   host threads
     |
  hot ABI
```

It should feel like booting directly into OSAura, except that the machine underneath is Windows.

Native OSAura remains the reference implementation. The emulator must reproduce its observable ABI and shell behavior rather than invent a Windows-specific JX environment.

## Primary interface

The normal case is simply:

```text
osaura.exe
```

That enters the interactive emulator shell.

Useful launch forms may include:

```text
osaura.exe
osaura.exe program.64B
osaura.exe --jx source.jx
osaura.exe --image dev.img
osaura.exe --root C:\OSAuraRoot
osaura.exe --diagnostic
```

But the interactive prompt is the primary product surface.

## Emulator boot sequence

`osaura.exe` should perform an OSAura-like hosted boot:

```text
Windows process start
        ↓
initialize hosted OSAura hot table
        ↓
initialize security subjects
        ↓
initialize clock/memory
        ↓
mount Windows-backed VFS root
        ↓
initialize task/job table
        ↓
initialize JX Book runtime
        ↓
run security admission self-test
        ↓
create terminal session
        ↓
OSAURA>
```

Expected startup text should deliberately resemble native OSAura diagnostics so developers can compare both environments easily.

## Console behavior

The emulator should work in:

- classic `cmd.exe`
- PowerShell
- Windows Terminal
- VS Code integrated terminal

Use the Windows console directly for the first implementation. ConPTY can be added where richer terminal behavior is needed.

Required behavior:

- normal keyboard input
- line editing
- command history
- Ctrl+C / interrupt handling
- terminal resize detection
- ANSI/VT output where available
- foreground/background OSAura job state
- multiple emulated terminal sessions later

The terminal shown to the user is an OSAura terminal. Windows console APIs are only the backend.

## OSAura hot ABI

The emulator must preserve the same hot-call layout as native OSAura:

```text
1bbbssss                  -> one-byte hot operation
0fffffff xxxxxxxx         -> two-byte extended operation
```

Current logical map remains canonical:

```text
80-87 STORAGE
88-8F IPC
90-97 NETWORK
98-9F JOBS
A0-A7 INPUT
A8-AF TERMINAL
B0-B7 USB
B8-BF WIFI
C0-C7 CLOCK
C8-CF MEMORY
D0-D7 TASK
D8-DF VFS
E0-E7 BOOK
E8-EF SECURITY
F0-FF RESERVED
```

A `.64B` program must not need recompilation merely because it moved between native OSAura and the Windows emulator.

## Hosted kernel mechanism mapping

### Terminal

```text
OSAura terminal calls
        ↓
Windows Console / ConPTY
```

### Clock

Use `QueryPerformanceCounter`, waitable timers, or suitable Windows timing primitives while returning OSAura clock semantics.

### Memory

Use normal process memory and `VirtualAlloc` where page-level behavior is required. Keep OSAura handles/ownership above raw Windows addresses where appropriate.

### VFS

Expose an OSAura namespace instead of Windows paths directly.

Suggested first mount:

```text
/              -> isolated OSAura emulator root
/host/c        -> C:\
/host/d        -> D:\ when present
/tmp           -> emulator temp directory
```

Win32 `HANDLE` values must never become OSAura/JX handles.

### Storage

Virtual block devices should normally be ordinary Windows image files.

```text
dev.img
   ↓
Windows file
   ↓
OSAura block driver facade
   ↓
80-87 storage shadows
```

Do not permit raw physical-drive access by default.

### Tasks/jobs

OSAura tasks retain:

- task ID
- security subject
- role
- state
- foreground/background status
- ticks/switch counters where meaningful

Windows threads/fibers may implement execution, but Windows thread IDs must not leak into the OSAura ABI.

### IPC

Use in-process queues first. Named pipes/shared memory can provide cross-process emulated IPC later.

### Network

Use Winsock behind the OSAura network ABI. `NETWORK` capability enforcement stays above Winsock.

### Wi-Fi / USB

Only expose operations Windows can faithfully provide. Never manufacture native-hardware success.

Saved Wi-Fi credential access remains separately protected by `WIFI_CREDENTIAL`.

## JX / `.64B`

The Windows emulator should be one of the primary development environments for JX.

Expected interaction:

```text
C:\> osaura.exe
OSAURA> jx hello.jx
JX: COMPILED hello.64B
OSAURA> run hello.64B
Hello from OSAura
OSAURA>
```

Or:

```text
C:\> osaura.exe app.64B
```

The same Book verifier, Bag model, generation swap rules, prepared calls, reactions, and hot-register semantics should be shared with native OSAura wherever practical.

## Shell parity

The emulator shell should intentionally track native OSAura commands.

Initial commands:

```text
HELP
ABOUT
MEM
VM
TASKS
TICKS
ALLOC
CLEAR
HALT
```

Hosted additions can be clearly marked, for example:

```text
HOST
MOUNTS
WINPATH
EXIT
```

`HALT` in hosted mode terminates the emulated machine/process cleanly rather than attempting to halt the Windows CPU.

## Isolation

The emulator should not treat the current Windows user as OSAura kernel subject `0` for ordinary JX programs.

Hosted boot creates the same logical authority split:

```text
subject 0 -> emulator kernel/backend
subject 1 -> JX runtime
other subjects -> programs/services
```

All normal OSAura capability checks remain active.

## Debug/development advantage

The command-line emulator should make OSAura substantially easier to develop:

- no reboot required
- no QEMU window required for normal JX work
- stdout/stderr diagnostics available immediately
- debugger attachable to `osaura.exe`
- sanitizer/debug builds possible
- native-vs-emulator conformance tests easy to automate
- `.64B` programs can be tested from ordinary Windows terminals

## Conformance rule

For every hosted implementation:

```text
same OSAura call
      |
      +-- native kernel
      +-- Windows emulator

same observable semantic result
```

Timing and hardware implementation may differ. ABI meaning, security decisions, handle ownership, Book behavior, and error semantics must match.

Unsupported native-only hardware operations must return an explicit unsupported result.

## Performance rule

Do not translate the full OSAura operation through strings on every call.

```text
resolve Windows resource once
        ↓
bind OSAura handle/shadow
        ↓
repeat numeric/prelinked path
```

The command-line emulator should therefore preserve the same core rule as native OSAura:

**resolve cold → bind once → execute hot.**

## First implementation milestone

Build `host/windows/osaura.exe` that:

1. starts in a normal Windows command-line window;
2. initializes the OSAura 128-entry hot table;
3. initializes security, clock, memory, task, VFS, Book, and terminal mechanisms;
4. prints OSAura boot/status lines;
5. presents `OSAURA>`;
6. supports the native basic shell commands;
7. can load a `.64B` Book;
8. maps an isolated Windows directory as the emulated root;
9. exits cleanly with `HALT` or `EXIT`;
10. has a Windows CI build and a scripted console smoke test.

That executable is the desired WSL-like developer experience: **OSAura running interactively inside a Windows command-line window.**
