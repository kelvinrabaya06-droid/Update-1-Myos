# MyOS 1

A 32-bit x86 operating system built from scratch, with no libraries and no OS underneath.
It boots straight into a graphical desktop in the style of Windows 95/98.

> A hobby x86 operating system written from scratch in C and assembly. Boots in QEMU into a
> Windows 95-style graphical desktop with a mouse, windows, taskbar, Start menu, setup wizard
> and built-in apps.

<!-- Add a screenshot here once you have run it, e.g. ![MyOS desktop](docs/desktop.png) -->

## Features
- **Bootable kernel:** Multiboot-compliant, written in C and x86 assembly, runs in QEMU
- **Graphics:** 800x600 true-color framebuffer driver (Bochs/QEMU VBE) with a custom bitmap
  font renderer and double buffering
- **Input:** PS/2 keyboard and mouse drivers, written against the hardware ports
- **Window manager:** overlapping windows with z-ordering, dragging, minimise/close,
  a taskbar and a Start menu
- **Setup wizard:** a multi-page installer with license, user name and program selection
- **Apps:** Notepad, Calculator, Paint, Terminal, Snake and a Software Center
- **Timing and clock:** millisecond timer from the PIT and a real-time clock reader
- **Fallback:** text-mode desktop if no graphics adapter is found

## Tech
C, x86 assembly (AT&T syntax), GNU toolchain, linker scripts, QEMU

## Build and run
    sudo apt install gcc gcc-multilib make qemu-system-x86   # Debian/Ubuntu/Mint
    make
    make run        # = qemu-system-i386 -kernel myos.elf -vga std -rtc base=localtime

Click inside the QEMU window to capture the mouse; press Ctrl+Alt+G to release it.

## How to use it
- Walk through the Setup wizard on first boot (Enter = Next).
- Double-click desktop icons, or open apps from the Start menu.
- Drag windows by their title bar; use the taskbar to switch or minimise.
- Open the Software Center to install or remove programs.
- Terminal commands: help, clear, echo, ver, time, cpu, apps, exit, reboot, shutdown

## Project layout
| File | Purpose |
|---|---|
| `boot.s` | Multiboot header, stack setup, jump into C |
| `kernel.c` | Kernel entry point and the text-mode desktop (fallback) |
| `gui.c` | Graphics, mouse, window manager, setup wizard, taskbar and all apps |
| `font.h` | 8x16 bitmap font, generated from IBM Plex Mono (SIL OFL 1.1) |
| `linker.ld` | Places the kernel at 1 MiB |
| `Makefile` | Build and run targets |

## Limitations
There is no disk driver or filesystem yet. Programs are built into the kernel, and the setup
wizard and Software Center control which ones are shown, so "installation" is simulated and
resets on reboot. Next steps: interrupts, a disk driver, a filesystem, and a loader for real
executables.

## Credits
The look imitates classic Windows 9x. All code is original, and no Microsoft assets are used.
