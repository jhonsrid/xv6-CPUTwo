// CPUTwo physical memory layout
//
// 0x00000000 -- kernel loads here (entry point, text, data)
// 0x03EFFFFF -- top of usable RAM (63 MB minus MMIO region)
// 0x03F00000 -- MMIO base (UART, timer, IC, block device, CPU control regs)
// 0x03FFFFFF -- top of 64 MB address space
//
// Kernel virtual memory layout (after paging enabled, Sv32):
// 0x00000000..PHYSTOP      -- direct-mapped kernel physical memory
// 0x03EFF000               -- boot stack page (entry.S sp=0x03EFFFFC)
// 0x03EFE000 = TRAMPOLINE  -- trampoline page (same VA in kernel and user)
// 0x03EFD000 = TRAPFRAME   -- per-process trap frame
// 0x03EFC000 = KSTACK(0)   -- kernel stack for proc 0 (guard at 0x03EFB000)
//  ...
// 0x03E7E000 = KSTACK(63)  -- kernel stack for proc 63
// 0x03F00000               -- MMIO_BASE (bypasses MMU in emulator)

// UART registers
#define UART0      0x03F00000u
#define UART0_IRQ  2u   // IC pending bit 1 (IC_BIT_UART_RX)

// Block device (replaces virtio)
#define VIRTIO0      0x03F03000u
#define VIRTIO0_IRQ  8u  // IC pending bit 3 (IC_BIT_BLKDEV)

// The kernel loads at physical address 0.
#define KERNBASE  0x00000000u

// Top of identity-mapped kernel RAM.  Physical RAM extends to 0x03F00000
// (MMIO base), but the identity map must stop before the KSTACK/TRAMPOLINE
// VA region (0x03E7E000+) to avoid remap conflicts.  KSTACK(63) is the
// lowest kernel-stack VA, so PHYSTOP = KSTACK(NPROC-1).
#define PHYSTOP   0x03E7E000u

// Trampoline is mapped at the top of every address space (kernel + user).
// CPUTwo: must be below MMIO_BASE (0x03F00000) because the emulator bypasses
// the MMU for any VA >= MMIO_BASE, turning the VA directly into a PA.
// Boot stack is at 0x03EFF000 (entry.S sets sp=0x03EFFFFC, one page).
// TRAMPOLINE sits just below it; KSTACK(p) = TRAMPOLINE - (p+1)*2*PGSIZE.
// KSTACK(0)=0x03EFC000 ... KSTACK(63)=0x03E7E000, all below MMIO_BASE.

// WARNING: This MUST match the address in trampoline.S
#define TRAMPOLINE  0x03EFE000u

// Kernel stacks: each process gets one page, with an invalid guard page above.
#define KSTACK(p)   (TRAMPOLINE - ((p)+1) * 2 * PGSIZE)

// User memory layout (low VA):
//   text
//   data / bss
//   fixed-size stack
//   expandable heap
//   ...
//   TRAPFRAME  (p->trapframe, written by kernel, read/written by trampoline)
//   TRAMPOLINE
#define TRAPFRAME   (TRAMPOLINE - PGSIZE)
