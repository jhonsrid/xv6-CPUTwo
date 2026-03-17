#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "cputwo.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline_start[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec(void);

extern int devintr(void);
void clockintr(void);

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// CPUTwo: EVEC is a table of handler function addresses.
// We keep two tables: one pointing everything at uservec (user mode),
// and one pointing everything at kernelvec (kernel mode / early boot).
// trapinithart() is called once per CPU to write the EVEC base address.
// prepare_return() updates entries before returning to user space.

// The EVEC table lives in start.c (static uint32 evec_table[16]).
// Expose the uservec address so prepare_return() can install it.

void
trapinithart(void)
{
  // Nothing to do: EVEC is set up in start.c and uservec entries are
  // installed by prepare_return() before first return to user mode.
  // (On RISC-V this set stvec = kernelvec; on CPUTwo, kernelvec is
  // unreachable in supervisor mode due to double-fault halting.)
}

//
// Handle an interrupt, exception, or system call from user space.
// Called from uservec in trampoline.S.
// Returns the user SATP value for trampoline's userret to switch to.
//
uint32
usertrap(void)
{
  struct proc *p = myproc();

  // Read the cause from the supervisor CAUSE register.
  uint32 cause = r_cause();

  // Save the user PC (already saved to trapframe->epc by uservec, but
  // also read here for the syscall case where we need to advance past
  // the SYSCALL instruction).
  // EPC was written to trapframe->epc in uservec; nothing else to do here.

  int which_dev = 0;

  if(cause == CAUSE_SYSCALL) {
    // System call.  EPC already points to PC+4 (return address past SYSCALL).

    if(killed(p))
      kexit(-1);

    // Re-enable... actually on CPUTwo we keep interrupts disabled in supervisor
    // mode (any interrupt in supervisor mode halts via double-fault).
    // Do NOT call intr_on() here.

    syscall();

  } else if((which_dev = devintr()) != 0) {
    // Device interrupt — handled by devintr().

  } else if((cause == CAUSE_SPGFAULT || cause == CAUSE_LPGFAULT) &&
            vmfault(p->pagetable, r_badaddr(), (cause == CAUSE_LPGFAULT) ? 1 : 0) != 0) {
    // Page fault on lazily-allocated page.

  } else {
    printf("usertrap(): unexpected cause 0x%x pid=%d\n", cause, p->pid);
    printf("            epc=0x%x badaddr=0x%x\n", p->trapframe->epc, r_badaddr());
    setkilled(p);
  }

  if(killed(p))
    kexit(-1);

  // Give up the CPU if this is a timer interrupt or the timer has
  // elapsed since the last check (timer is polled, not interrupt-driven,
  // because CPUTwo hardware clobbers lr on trap entry).
  if(which_dev == 2)
    yield();
  if(cause == CAUSE_SYSCALL && (mmio_r(IC_PENDING) & IC_BIT_TIMER)) {
    clockintr();
    yield();
  }

  prepare_return();

  // Return the user SATP so userret can switch page tables.
  return MAKE_SATP(p->pagetable);
}

//
// Set up trapframe and supervisor registers for a return to user space.
//
void
prepare_return(void)
{
  struct proc *p = myproc();

  // Interrupts stay off in supervisor mode (CPUTwo double-fault rule).
  // Do not call intr_on() or intr_off() here.

  // Update the EVEC table so all traps go to uservec while user code runs.
  // uservec is at TRAMPOLINE + (uservec - trampoline).
  uint32 trampoline_uservec = TRAMPOLINE + (uint32)(uservec - trampoline_start);

  // Write all EVEC slots (including hardware interrupt, cause 6) to uservec.
  // EVEC is stored in evec_table[] in start.c; its address is in REG_EVEC.
  uint32 *evec = (uint32 *)r_evec();
  for(int i = 0; i < 10; i++)
    evec[i] = trampoline_uservec;

  // Fill trapframe fields that uservec needs next time this process traps.
  p->trapframe->kernel_satp   = r_satp();
  p->trapframe->kernel_sp     = p->kstack + PGSIZE;
  p->trapframe->kernel_trap   = (uint32)usertrap;
  p->trapframe->kernel_hartid = 0;  // CPUTwo single-CPU; r11 is frame pointer in C ABI

  // Set up supervisor registers for SYSRET:
  //   REG_EPC  = user return address (already in trapframe->epc; write MMIO)
  //   REG_ESTATUS = STATUS_IE (bit 1) — SYSRET forces bit 0 clear (user mode)
  //                 so user resumes with interrupts enabled.
  w_epc(p->trapframe->epc);
  mmio_w(REG_EFLAGS, 0);                   // restore user flags as 0
  mmio_w(REG_ESTATUS, STATUS_IE);          // user gets IE=1, supervisor=0
}

//
// Called from kernelvec.S on early-boot kernel-mode exceptions.
// In normal operation, CPUTwo halts before this runs (double-fault rule).
//
void
kerneltrap(void)
{
  uint32 cause  = r_cause();
  uint32 epc    = r_epc();
  uint32 baddr  = r_badaddr();

  printf("kerneltrap: cause=0x%x epc=0x%x badaddr=0x%x\n", cause, epc, baddr);
  panic("kerneltrap");
}

void
clockintr(void)
{
  if(cpuid() == 0){
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
  }

  // Acknowledge the timer interrupt in the IC.
  mmio_w(IC_ACK, IC_BIT_TIMER);

  // The timer is periodic; it automatically reloads — no need to reprogram.
}

//
// Check for device interrupts (cause 6) and handle them.
// Returns 2 for timer interrupt, 1 for other device, 0 if not recognised.
//
int
devintr(void)
{
  uint32 cause = r_cause();

  if(cause == CAUSE_IRQ) {
    uint32 pending = mmio_r(IC_PENDING);

    if(pending & IC_BIT_TIMER) {
      clockintr();
      return 2;
    } else if(pending & IC_BIT_UART_RX) {
      uartintr();
      mmio_w(IC_ACK, IC_BIT_UART_RX);
      return 1;
    } else if(pending & IC_BIT_UART_TX) {
      uartintr();   // handles TX completion too
      mmio_w(IC_ACK, IC_BIT_UART_TX);
      return 1;
    } else if(pending & IC_BIT_BLKDEV) {
      virtio_disk_intr();
      mmio_w(IC_ACK, IC_BIT_BLKDEV);
      return 1;
    } else if(pending) {
      printf("unexpected interrupt pending=0x%x\n", pending);
    }
    return 1;
  }

  return 0;
}

//
// Poll for and handle pending device events from supervisor mode.
// Called from the scheduler's idle loop because CPUTwo cannot take
// real interrupts in supervisor mode (double-fault halts the CPU).
//
// We check UART status directly rather than IC pending bits because
// the UART RX interrupt is edge-triggered: ACKing the IC pending bit
// after reading could race with a newly arrived byte, losing it.
//
void
polldev(void)
{
  // Timer: check IC pending (level-based; timer auto-reloads).
  if(mmio_r(IC_PENDING) & IC_BIT_TIMER) {
    clockintr();
  }

  // UART: check the device status register directly.
  if(mmio_r(UART_STATUS) & UART_STATUS_RX_AVAIL) {
    // Drain all available characters.
    while(mmio_r(UART_STATUS) & UART_STATUS_RX_AVAIL) {
      int c = mmio_r(UART_RX) & 0xFF;
      consoleintr(c);
    }
    // Clear the IC pending bit (if any) now that we've drained.
    mmio_w(IC_ACK, IC_BIT_UART_RX);
    return;
  }

  // Nothing pending — tell the emulator to sleep until an event arrives.
  // This prevents the host from burning 100% CPU on the idle loop.
  mmio_w(REG_WFI, 0);
}
