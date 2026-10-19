# Nova Claude OS

A from-scratch 32-bit x86 operating system with a graphical desktop, written in C and x86 assembly.
It boots from GRUB (Multiboot), drives a linear framebuffer directly, and ships a window manager and
eight apps. Built in conversation with Claude (Anthropic).

## Features
- **Kernel:** GDT/IDT, PIC and 100 Hz PIT timer, PS/2 keyboard and mouse drivers, CMOS clock,
  heap allocator, in-memory filesystem with ATA/IDE disk persistence, ACPI-style shutdown/reboot,
  crash screen for CPU exceptions.
- **Desktop:** wallpapers, accent colors, taskbar with clock, Start menu, draggable / resizable /
  maximizable windows, Alt+Tab, Alt+F4, HiDPI (2x pixel-doubled) display modes.
- **Apps:** Terminal (about 35 commands, tab completion), File Manager, Notepad, Calculator, Paint,
  Snake, System Monitor (with End task), Settings.

## Build
Needs `gcc` with 32-bit support (`-m32`), `binutils`, `grub-pc-bin`, `xorriso`, `mtools`:

    make            # produces NovaClaudeOS.iso
    make clean

On Debian/Ubuntu: `sudo apt install build-essential gcc-multilib grub-pc-bin xorriso mtools`.

## Run
**Any machine, including Apple Silicon Macs (QEMU):**

    qemu-img create -f raw disk.img 64M      # optional, lets files persist
    qemu-system-i386 -cdrom NovaClaudeOS.iso -boot d -m 128 -vga std -display cocoa,zoom-to-fit=on -hda disk.img

(Use `-display gtk` or `sdl` instead of `cocoa` on Linux/Windows.) At the boot menu choose a screen size:
Large 1920x1080 (default), Extra large 2560x1440, or Standard 1024x768. Click in the window to capture
the mouse; Ctrl+Alt+G releases it.

**VirtualBox (Intel/AMD PCs only):** type Other/Unknown (32-bit), 128 MB+ RAM, 16 MB video memory,
BIOS boot (EFI off), pointing device PS/2 Mouse, ISO in the optical drive.

## Saving files
Without a disk, files live in RAM and vanish on reboot. Attach an IDE disk (VirtualBox: IDE controller,
not SATA; QEMU: `-hda`). A blank disk is initialised automatically; changes are saved about 2 seconds
after you make them and on shutdown. Terminal: `disk` (status), `sync` (save now). A disk that already
holds other data is never touched; `format yes` erases it and sets it up for Nova Claude OS.

## Layout
| File | Purpose |
|---|---|
| `boot.s` | Multiboot header (requests screen size), GDT, FPU init, interrupt stubs |
| `kernel.c` | libc basics, heap, IDT/PIC/PIT, keyboard and mouse, RTC, ATA driver, filesystem |
| `gfx.c` | Framebuffer, drawing primitives, text, icons, wallpapers, cursor, HiDPI scaling |
| `wm.c` | Window manager, desktop, taskbar, Start menu, main loop |
| `app_*.c` | The apps |
| `font.h` | Terminus 8x16 bitmap font |

## Limitations
32-bit BIOS boot only (no UEFI), PS/2 input only (no USB), IDE disks only (no SATA/AHCI), no networking,
single user, no process isolation (apps run inside the kernel as windows).

## Credits
Font: [Terminus](https://terminus-font.sourceforge.net/) by Dimitar Zhekov, SIL Open Font License 1.1.
