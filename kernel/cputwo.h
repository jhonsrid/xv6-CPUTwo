// CPUTwo architecture definitions.
// Replaces riscv.h for the CPUTwo port.

// TCC doesn't have __sync_synchronize; on single-core CPUTwo a compiler
// barrier is sufficient for ordering.
#define __sync_synchronize() asm volatile("" ::: "memory")

// ---------------------------------------------------------------------------
// Page / address constants (usable in both C and assembler)
// ---------------------------------------------------------------------------

#define PGSIZE   4096
#define PGSHIFT  12

#define PGROUNDUP(sz)   (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a)  ((a) & ~(PGSIZE-1))

// Sv32: two 10-bit VPN fields + 12-bit page offset.
// MAXVA = 2^31 — keep top bit clear to avoid sign-extension issues.
#define MAXVA (1u << 31)

// Sv32 VPN extraction (10-bit fields)
#define PXMASK         0x3FF          // 10 bits
#define PXSHIFT(level) (PGSHIFT + (10*(level)))
#define PX(level, va)  ((((uint32)(va)) >> PXSHIFT(level)) & PXMASK)

// ---------------------------------------------------------------------------
// PTE bits (Sv32, 32-bit PTE)
// ---------------------------------------------------------------------------

#define PTE_V  (1u << 0)  // valid
#define PTE_D  (1u << 1)  // dirty (set by hardware on store)
#define PTE_R  (1u << 5)  // readable
#define PTE_W  (1u << 6)  // writable
#define PTE_X  (1u << 7)  // executable
#define PTE_U  (1u << 8)  // user-accessible
#define PTE_G  (1u << 9)  // global (not flushed by SFENCE/SATP write)

// Shift a physical address into the PPN field of a PTE.
// PTE[31:12] = PA[31:12], i.e. PPN = PA >> 12.
#define PA2PTE(pa)    ((((uint32)(pa)) >> 12) << 12)
#define PTE2PA(pte)   (((pte) >> 12) << 12)
#define PTE_FLAGS(pte) ((pte) & 0xFFF)

// ---------------------------------------------------------------------------
// Supervisor-mode MMIO registers (0x03FFF000 region)
// User-mode access to these raises a bus error (cause 0x02).
// ---------------------------------------------------------------------------

#define REG_EPC     0x03FFF000u  // saved PC on trap entry
#define REG_EFLAGS  0x03FFF004u  // saved flags on trap entry
#define REG_EVEC    0x03FFF008u  // exception vector table base address
#define REG_CAUSE   0x03FFF00Cu  // most recent exception cause (read-only)
#define REG_STATUS  0x03FFF010u  // bit0=supervisor, bit1=IE
#define REG_ESTATUS 0x03FFF014u  // STATUS snapshot saved on trap entry
#define REG_SATP    0x03FFF018u  // MMU control: bit31=EN, bits[19:0]=PPN
#define REG_BADADDR 0x03FFF01Cu  // faulting VA on page fault (read-only)

// STATUS / ESTATUS bits
#define STATUS_SUPERVISOR  0x01u  // bit 0: current privilege (1=supervisor)
#define STATUS_IE          0x02u  // bit 1: interrupts enabled

// SATP bits
#define SATP_EN  (1u << 31)

// Build a SATP value from a physical page-table address
#define MAKE_SATP(pt)  (SATP_EN | (((uint32)(pt)) >> 12))

// ---------------------------------------------------------------------------
// Exception cause codes
// ---------------------------------------------------------------------------

#define CAUSE_ILLEGAL   0x00u  // illegal instruction / privileged insn in user mode
#define CAUSE_MISALIGN  0x01u  // misaligned memory access
#define CAUSE_BUSERR    0x02u  // bus error (out-of-range address)
#define CAUSE_SYSCALL   0x03u  // SYSCALL instruction from user mode
#define CAUSE_DIVZERO   0x04u  // division by zero
#define CAUSE_HALT      0x05u  // HALT instruction
#define CAUSE_IRQ       0x06u  // hardware interrupt
#define CAUSE_IPGFAULT  0x07u  // instruction page fault
#define CAUSE_LPGFAULT  0x08u  // load page fault
#define CAUSE_SPGFAULT  0x09u  // store page fault

// ---------------------------------------------------------------------------
// Interrupt controller (IC) at 0x03F02000
// ---------------------------------------------------------------------------

#define IC_BASE     0x03F02000u
#define IC_PENDING  0x03F02000u  // read-only: pending interrupt bits
#define IC_ENABLE   0x03F02004u  // read/write: enabled interrupt sources
#define IC_ACK      0x03F02008u  // write to clear pending bits

#define IC_BIT_TIMER    (1u << 0)
#define IC_BIT_UART_RX  (1u << 1)
#define IC_BIT_UART_TX  (1u << 2)
#define IC_BIT_BLKDEV   (1u << 3)

// ---------------------------------------------------------------------------
// UART at 0x03F00000
// ---------------------------------------------------------------------------

#define UART_BASE    0x03F00000u
#define UART_STATUS  (UART_BASE + 0x00u)  // bit0=TX ready, bit1=RX available
#define UART_TX      (UART_BASE + 0x04u)  // write byte here to transmit
#define UART_RX      (UART_BASE + 0x08u)  // read received byte
#define UART_CTRL    (UART_BASE + 0x0Cu)  // bit0=RX irq enable, bit1=TX irq enable

#define UART_STATUS_TX_READY  (1u << 0)
#define UART_STATUS_RX_AVAIL  (1u << 1)
#define UART_CTRL_RX_IRQ      (1u << 0)
#define UART_CTRL_TX_IRQ      (1u << 1)

// ---------------------------------------------------------------------------
// Timer at 0x03F01000
// ---------------------------------------------------------------------------

#define TIMER_PERIOD  0x03F01000u  // write=set period & restart; read=remaining
#define TIMER_CTRL    0x03F01004u  // bit0=enable, bit1=irq enable

// ---------------------------------------------------------------------------
// Block device at 0x03F03000
// ---------------------------------------------------------------------------

#define BLK_SECTOR  0x03F03000u  // sector number
#define BLK_BUFFER  0x03F03004u  // guest memory address (512-byte aligned)
#define BLK_CMD     0x03F03008u  // 1=read, 2=write
#define BLK_STATUS  0x03F0300Cu  // 0=idle, 1=busy, 2=error
#define BLK_CTRL    0x03F03010u  // bit0=irq enable

#define BLK_CMD_READ   1u
#define BLK_CMD_WRITE  2u
#define BLK_STATUS_IDLE  0u
#define BLK_STATUS_BUSY  1u
#define BLK_STATUS_ERR   2u

// ---------------------------------------------------------------------------
// Inline helpers (C only, not assembler)
// ---------------------------------------------------------------------------

#ifndef __ASSEMBLER__

#include "types.h"

// Read/write a 32-bit MMIO register.
static inline uint32 mmio_r(uint32 addr) {
  return *(volatile uint32 *)addr;
}
static inline void mmio_w(uint32 addr, uint32 val) {
  *(volatile uint32 *)addr = val;
}

// Supervisor register accessors
static inline uint32 r_cause(void)   { return mmio_r(REG_CAUSE);   }
static inline uint32 r_epc(void)     { return mmio_r(REG_EPC);     }
static inline void   w_epc(uint32 v) { mmio_w(REG_EPC, v);         }
static inline uint32 r_badaddr(void) { return mmio_r(REG_BADADDR); }
static inline uint32 r_status(void)  { return mmio_r(REG_STATUS);  }
static inline void   w_status(uint32 v) { mmio_w(REG_STATUS, v);   }
static inline uint32 r_satp(void)    { return mmio_r(REG_SATP);    }
static inline void   w_satp(uint32 v)   { mmio_w(REG_SATP, v);     }
static inline uint32 r_evec(void)    { return mmio_r(REG_EVEC);    }
static inline void   w_evec(uint32 v)   { mmio_w(REG_EVEC, v);     }

// Enable/disable interrupts
static inline void intr_on(void) {
  mmio_w(REG_STATUS, mmio_r(REG_STATUS) | STATUS_IE);
}
static inline void intr_off(void) {
  mmio_w(REG_STATUS, mmio_r(REG_STATUS) & ~STATUS_IE);
}
static inline int intr_get(void) {
  return (mmio_r(REG_STATUS) & STATUS_IE) != 0;
}

// TLB flush via SFENCE instruction (flushes all non-global TLB entries).
// opcode 0x3E, all other bits zero.
static inline void sfence_vma(void) {
  asm volatile(".word 0x3E000000" : : : "memory");
}

// Stack pointer (r13)
static inline uint32 r_sp(void) {
  uint32 x;
  asm volatile("mov %0, r13" : "=r"(x));
  return x;
}

// xv6 stores cpuid in r11 (callee-saved, not used by C ABI for other purposes).
// (There is no dedicated tp/hartid register on CPUTwo.)
static inline uint32 r_tp(void) {
  uint32 x;
  asm volatile("mov %0, r11" : "=r"(x));
  return x;
}
static inline void w_tp(uint32 x) {
  asm volatile("mov r11, %0" : : "r"(x));
}

// PTE type and page table type
typedef uint32 pte_t;
typedef uint32 *pagetable_t;  // 1024 PTEs per level

#endif // __ASSEMBLER__
