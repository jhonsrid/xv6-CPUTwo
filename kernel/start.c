#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "cputwo.h"
#include "defs.h"

void main(void);

// One 4096-byte stack per CPU, used before the kernel heap is up.
__attribute__((aligned(16))) char stack0[4096 * NCPU];

// Exception handler addresses: one entry per cause code (10 causes, 0x00–0x09).
// Filled by start(); written to REG_EVEC so the hardware can dispatch.
static uint32 evec_table[16];

// Forward declarations for trap entry points defined in trampoline.S/kernelvec.S
void uservec(void);     // user-mode trap entry (trampoline.S)
void kernelvec(void);   // kernel-mode trap entry (kernelvec.S)

// entry.S jumps here in supervisor mode on the boot stack.
void
start(void)
{
  // Disable MMU (SATP.EN = 0) — physical addressing until kvminit().
  mmio_w(REG_SATP, 0);

  // Build the exception vector table.
  // Causes 0x00–0x05 (illegal/misalign/buserr/syscall/divzero/halt) and
  // page faults (0x07–0x09) will be dispatched to kernelvec initially;
  // trapinithart() switches user-trap causes to uservec via stvec equivalent.
  // For now point everything at kernelvec so we get a clean panic on any trap
  // before full initialisation is done.
  for(int i = 0; i < 16; i++)
    evec_table[i] = (uint32)kernelvec;

  // Write the EVEC table base address to the supervisor EVEC register.
  // EVEC slot 15 is reserved for the kerneltrap function pointer,
  // set later by trapinit().
  mmio_w(REG_EVEC, (uint32)evec_table);

  // Keep each CPU's hart id in r11 (our "tp" equivalent).
  // entry.S already set r11 = 0 for hart 0; nothing to do here for
  // single-CPU, but in principle we'd read a per-CPU MMIO register.
  // (w_tp() is a no-op that just writes r11; already correct from entry.S)

  // Jump to main().
  main();
}

// ---------------------------------------------------------------------------
// Timer initialisation — called from main() after consoleinit/printfinit.
// ---------------------------------------------------------------------------
void
timerinit(void)
{
  // Start the periodic timer with interrupts enabled.
  // The IC mask for the timer is set in plicinit().
  mmio_w(TIMER_PERIOD, 100000);
  mmio_w(TIMER_CTRL, 0x3);  // bit0=enable, bit1=irq enable
}
