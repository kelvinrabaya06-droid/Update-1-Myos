# MyOS 0.3

A tiny 32-bit x86 operating system written from scratch: a graphical desktop in the
style of Windows 95/98, a first-boot Setup wizard, and a Software Center for
installing and removing programs.

## Build & run
    sudo apt install gcc gcc-multilib make qemu-system-x86   # Debian/Ubuntu/Mint
    make
    make run        # = qemu-system-i386 -kernel myos.elf -vga std -rtc base=localtime

Click inside the QEMU window to capture the mouse; press Ctrl+Alt+G to release it.

## What you get
- 800x600 true-color desktop (Bochs/QEMU VBE framebuffer), PS/2 mouse and keyboard
- Setup wizard: welcome, license, your name, choose programs, install, finish
- Taskbar with Start button, window buttons and a clock; Start menu with your name
- Draggable, stackable, minimizable windows
- Apps: Notepad, Calculator, Paint, Terminal, Snake, Software Center, About
- Double-click desktop icons, or use the Start menu
- If no VBE display is found, it falls back to the older text-mode desktop

## About "installing"
There is no disk driver or filesystem yet. The programs are built into the kernel;
Setup and the Software Center decide which ones are "installed" (shown on the desktop
and Start menu). That choice lives in memory and resets when you reboot.

## Files
- boot.s    Multiboot header + stack, jumps to C
- kernel.c  Text-mode desktop (fallback) and the kernel entry point
- gui.c     Graphics, mouse, windows, setup wizard, taskbar, all the apps
- font.h    8x16 bitmap font (generated from IBM Plex Mono, SIL OFL 1.1)
- linker.ld Places the kernel at 1 MiB
