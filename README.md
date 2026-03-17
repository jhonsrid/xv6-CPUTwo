# xv6-CPUTwo

A port of [xv6](https://pdos.csail.mit.edu/6.1810/), MIT's teaching operating system, from RISC-V to [CPUTwo](https://github.com/jhonsrid/CPUTwo) — a custom 32-bit RISC architecture with a software emulator.

See [README.original](README.original) for the upstream xv6 acknowledgments and credits.

---

## What is xv6?

xv6 is a re-implementation of Dennis Ritchie's and Ken Thompson's Unix Version 6, written in ANSI C for modern hardware. It was created at MIT for teaching operating systems and covers virtual memory, processes, file systems, pipes, and a simple shell — all in roughly 10,000 lines of code.

The upstream version targets RISC-V and runs on QEMU. This port targets CPUTwo and runs on its emulator.

---

## What is CPUTwo?

CPUTwo is a minimal 32-bit RISC CPU with a 64 MB address space, Sv32-compatible MMU, memory-mapped I/O, and two privilege modes (user/supervisor). See `architecture.md` in the `CPUTwo` repository for the full ISA specification.

Key differences from RISC-V that shaped the port:

- **32-bit only** — Sv32 page tables, 4 KB pages, no 64-bit types in hardware
- **lr (r14) clobbered on trap entry** — hardware destroys the link register on every exception/interrupt, so asynchronous interrupts cannot be used safely; all device I/O is polled
- **Double fault halts the CPU** — any exception in supervisor mode halts immediately, so interrupt handlers cannot run in kernel mode
- **MMIO bypass** — addresses >= `0x03F00000` always bypass the MMU, so device registers are accessible without page table entries
- **No WFI instruction** — idle hinting is done via a custom MMIO register (`0x03FFF020`)

---

## Building

Requires the [CPUTwo TCC cross-compiler](https://github.com/jhonsrid/tinycc_CPUTwo) and the [CPUTwo emulator](https://github.com/jhonsrid/CPUTwo).

```sh
make            # build kernel and user programs
make fs.img     # build the file system image
make run        # build and run in the emulator
```

---

## Running

```sh
~/CPUTwo/emulatortwo -blk fs.img kernel/kernel
```

You should see xv6 boot to a `$` shell prompt. Try `echo hello`, `ls`, `cat README`, `wc README`.

The emulator sets the terminal to raw mode when stdin is a TTY. Use **Ctrl-A z** to suspend and **Ctrl-A x** to quit (see the emulator README for details).

---

## Porting notes

The port was done incrementally with the help of Claude Code. The major changes from upstream xv6-riscv:

### Memory layout

- `KERNBASE` = `0x00000000` (kernel loads at address 0, not `0x80000000`)
- `PHYSTOP` = `0x03E7E000` (~62.5 MB usable RAM, stopping below the KSTACK/TRAMPOLINE VA region)
- `TRAMPOLINE` = `0x03EFE000` (below the MMIO region, not at `MAXVA`)
- `TRAPFRAME` = `0x03EFD000`
- MMIO region `0x03F00000`–`0x03FFFFFF` bypasses the MMU in hardware

### Interrupt handling — all polling

CPUTwo's architecture makes traditional interrupt handling impractical:

1. Hardware clobbers `lr` on every trap entry, destroying leaf-function return addresses
2. Any exception in supervisor mode causes an immediate halt (double fault)

All device I/O is therefore handled by **polling** rather than interrupts:

- **IC mask is zero** — no interrupt sources are unmasked in the interrupt controller
- **`polldev()`** in `trap.c` checks UART status and timer pending bits directly, called from the scheduler idle loop and on syscall entry
- **Console output** uses synchronous polling (`uartputc_sync`) instead of interrupt-driven TX
- **Block device** uses synchronous polling (spin on status register)
- **Preemption** happens at syscall boundaries by checking the timer pending bit, not via timer interrupts

### Syscall stubs

CPUTwo's `SYSCALL` instruction clobbers `lr`, so the user-space syscall stubs (`usys.pl`) save and restore `lr` on the stack around the `SYSCALL` instruction.

### ELF loading

TCC produces ELF binaries with non-page-aligned segments and dynamic linking artifacts. The port:

- Adds `-static` to all link commands
- Uses `-Wl,-e,start` to bypass TCC's built-in `_start` (which hardcodes SP to the MMIO base)
- Relaxes `exec.c` to accept non-page-aligned segment vaddrs
- Fixes `loadseg()` to handle intra-page offsets when loading segments

### Emulator enhancements

The port required several emulator additions:

- **Terminal raw mode** — keystrokes delivered immediately, no host echo
- **Ctrl-A escape prefix** — Ctrl-A z to suspend, Ctrl-A x to quit
- **WFI register** (`0x03FFF020`) — write to idle the host CPU until an event arrives, preventing 100% CPU usage when the guest is idle
