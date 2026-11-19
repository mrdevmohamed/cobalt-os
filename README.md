# Cobalt OS

A bootable, independent 32-bit x86 hobby operating system written in **C and FASM assembly**. It runs directly on emulated hardware, not on top of Linux.

## Build and run

On Debian/Ubuntu:

```sh
sudo apt install build-essential binutils fasm qemu-system-x86 python3
make
make run
```

Run these commands from this directory. GCC must support compiling freestanding i386 objects with `-m32`; the build does not link libc or require 32-bit libc development files.

`build/cobalt.img` is a 1.44 MB bootable **floppy disk image**, not an ISO. `make run` boots it with QEMU's standard VGA and 32 MB RAM. QEMU emulates BIOS, VGA, and a PS/2 keyboard/mouse. Do not write the image to a physical drive: physical hardware compatibility has not been established.

The graphical QEMU window accepts keyboard input. QEMU may capture the mouse; **Ctrl+Alt+G** releases capture. Close QEMU to exit, or enter `shutdown` in the OS terminal.

## Desktop and terminal

- 800×600, 32-bit color graphical desktop with the firmware's 8×16 font.
- Movable terminal window; drag its title bar.
- Click `_` to minimize. Click **Terminal** in the taskbar to toggle the window.
- Software mouse cursor and uptime clock.
- US keyboard layout, Shift/Caps Lock, Backspace, and Enter.
- Up/Down recalls eight recent commands and restores an unfinished draft.
- Ctrl+L clears the screen while keeping the current input.
- Ctrl+C cancels input; F1 shows command help.
- Input is limited to 79 characters. Output scrolls within the terminal; there is no scrollback viewer.

### Commands

| Command | Behavior |
| --- | --- |
| `help` | List commands |
| `clear` | Clear the terminal |
| `about`, `version` | Show OS version and architecture |
| `echo TEXT` | Print text |
| `uptime` | Show seconds since boot, from the PIT |
| `date` | Read the hardware RTC; QEMU normally uses UTC |
| `mem` | Report kernel/static-data footprint, not total installed RAM |
| `ls` | List compiled-in read-only files |
| `cat README.TXT` | Read a ROM file; names are case-sensitive |
| `cat COMMANDS.TXT` | Read command reference |
| `color white` | Set terminal text color; also `green` or `amber` |
| `history` | List the eight most recent submitted commands |
| `reboot` | Reset through the PS/2 controller |
| `shutdown` | Power off through QEMU's ACPI port; otherwise halt |
| `halt` | Stop the CPU until the machine is reset |

Commands are case-sensitive. There is no quoting, piping, executable loading, or host-shell execution.

## Verification

```sh
make smoke
```

The Python-standard-library-only smoke scenario boots the actual image in headless QEMU, sends emulated keyboard and mouse input through QMP, checks shell output on debug port `0xe9`, captures the framebuffer, and exercises shutdown. It covers editing at the input boundary, command history/drafts, ROM reads and missing-file errors, command errors, the RTC and timer, and minimizing/restoring/dragging the terminal.

Smoke runs use QEMU's disposable `-snapshot` disk overlay, leaving the base image unchanged and allowing verification alongside an already-running instance.

Generated files:

- `build/desktop.png`: initial desktop.
- `build/minimized.png`: desktop with terminal minimized.
- `build/smoke.png`: terminal after the interaction scenario.
- `build/smoke-debug.log`: guest terminal output and GUI diagnostic events.
- `build/kernel.elf`: symbols for debugging; the boot image uses the flat `kernel.bin`.

`make clean` removes generated artifacts.

## Source layout and boot contract

```text
boot/boot.asm       512-byte BIOS boot sector
boot/loader.asm     Disk loading, VBE setup, font copy, protected-mode transition
boot/entry.asm      C entry, BSS clearing, timer and fatal interrupt stubs
kernel/kernel.c    Hardware I/O, graphics, PS/2 input, desktop, terminal, shell
kernel/linker.ld   Fixed-address i386 memory layout
scripts/smoke.py   End-to-end QEMU interaction scenario
```

The disk layout is boot sector LBA 0, second-stage reservation LBA 1–8, and kernel starting at LBA 9. BIOS CHS reads assume 18 sectors per track and two heads. The build rejects flat kernels larger than 127 sectors (65,024 bytes); extending the OS beyond that requires changing the loader.

The loader enumerates VBE modes and requires an 800×600×32 linear framebuffer with eight-bit BGR channels. It copies the firmware font to `0x7000`, puts framebuffer address/pitch at `0x6000`, enables A20, and enters 32-bit protected mode. The kernel loads at `0x10000`; the stack starts at `0x90000`. The linker rejects BSS extending into the reserved stack region.

The kernel uses a flat GDT, an IDT, a remapped PIC, and a roughly 100 Hz PIT interrupt. PS/2 keyboard and three-byte mouse packets are polled between timer wakeups. Drawing goes directly to the framebuffer without a heap or full-screen backbuffer. Terminal cells and command history have fixed capacity.

## Deliberate limitations

This is a small bare-metal OS, **not a production desktop OS**. It has one kernel address space and no userspace, scheduler, memory protection, heap allocator, disk filesystem, persistent writes, networking, audio, USB stack, Unicode support, or application loader. ROM files are actual constant data compiled into the kernel, not files on the boot disk. Fatal CPU exceptions halt; recovery is not implemented.

Boot requires legacy BIOS, not UEFI-only firmware. The shutdown port is QEMU-specific. Only the QEMU machine configured by the Makefile is a verification target; real-machine support is unverified.
