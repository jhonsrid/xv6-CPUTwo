// CPUTwo interrupt controller (IC) driver.
// Replaces the RISC-V PLIC for the CPUTwo port.
//
// The IC sits at 0x03F02000:
//   +0x00  Pending  (read-only)  — bits set when device fires
//   +0x04  Enable               — write 1 to unmask a source
//   +0x08  Acknowledge          — write bit(s) to clear pending

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "cputwo.h"
#include "defs.h"

void
plicinit(void)
{
  // Enable UART RX and block device interrupts in the IC.
  // Timer is enabled separately after kernelvec is verified.
  // Enable timer, UART RX, and block device interrupts in the IC.
  // Enable UART RX and block device interrupts via the IC.
  // Timer is polled (not interrupt-driven) because the high-frequency
  // timer interrupt + KRET return sequence has a known interaction bug.
  mmio_w(IC_ENABLE, IC_BIT_UART_RX | IC_BIT_BLKDEV);
}

void
plicinithart(void)
{
  // Nothing per-hart on CPUTwo's simple IC.
  // (On RISC-V this configured the per-hart PLIC threshold and enable regs.)
}

// Returns the bitmask of the highest-priority pending+enabled interrupt,
// or 0 if none.  (Kept for API compatibility with callers.)
int
plic_claim(void)
{
  return (int)mmio_r(IC_PENDING);
}

// Acknowledge an interrupt by clearing the corresponding bit.
void
plic_complete(int irq_mask)
{
  mmio_w(IC_ACK, (uint32)irq_mask);
}
