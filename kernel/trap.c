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

// in kernelvec.S, handles kernel-mode traps.
void kernelvec(void);

extern int devintr(void);
void clockintr(void);
void kerneltrap(void);

void
trapinit(void)
{
  initlock(&tickslock, "time");

  // Store kerneltrap address in EVEC slot 15 so kernelvec.S can load
  // it via register-indirect call.
  uint32 *evec = (uint32 *)r_evec();
  evec[15] = (uint32)kerneltrap;
}

// Set EVEC entries to point at kernelvec (for supervisor-mode traps).
// Called once at boot.
void
trapinithart(void)
{
  uint32 *evec = (uint32 *)r_evec();
  for(int i = 0; i < 10; i++)
    evec[i] = (uint32)kernelvec;
}

//
// Handle an interrupt, exception, or system call from user space.
// Called from uservec in trampoline.S.
//
uint32
usertrap(void)
{
  struct proc *p = myproc();

  // Switch EVEC to kernelvec so traps during kernel execution are
  // handled by the kernel trap handler (not the user trampoline).
  uint32 *evec = (uint32 *)r_evec();
  for(int i = 0; i < 10; i++)
    evec[i] = (uint32)kernelvec;

  uint32 cause = r_cause();

  int which_dev = 0;

  if(cause == CAUSE_SYSCALL) {
    if(killed(p))
      kexit(-1);

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

  // Give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  // Timer is polled at syscall boundaries for preemption.
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

  // Disable interrupts — we are about to switch EVEC to uservec and
  // set up SYSRET state.  No traps should fire until we're in user mode.
  intr_off();

  // Update the EVEC table so all traps go to uservec while user code runs.
  uint32 trampoline_uservec = TRAMPOLINE + (uint32)(uservec - trampoline_start);
  uint32 *evec = (uint32 *)r_evec();
  for(int i = 0; i < 10; i++)
    evec[i] = trampoline_uservec;

  // Fill trapframe fields that uservec needs next time this process traps.
  p->trapframe->kernel_satp   = r_satp();
  p->trapframe->kernel_sp     = p->kstack + PGSIZE;
  p->trapframe->kernel_trap   = (uint32)usertrap;
  p->trapframe->kernel_hartid = 0;

  // Set up supervisor registers for SYSRET:
  //   REG_EPC  = user return address
  //   REG_ESTATUS = STATUS_IE — SYSRET forces bit 0 clear (user mode),
  //                 so user resumes with interrupts enabled.
  w_epc(p->trapframe->epc);
  mmio_w(REG_EFLAGS, 0);                   // restore user flags as 0
  mmio_w(REG_ESTATUS, STATUS_IE);          // user gets IE=1, supervisor=0
}

//
// Handle a trap from kernel mode.
// Called from kernelvec.S after saving caller-saved regs and EPC/ESTATUS/EFLAGS.
//
void
kerneltrap(void)
{
  uint32 cause = r_cause();

  if(devintr() != 0) {
    // Handled a device interrupt — return to interrupted code.
    return;
  }

  // Not a device interrupt — this is a real fault in kernel code.
  uint32 epc   = r_epc();
  uint32 baddr = r_badaddr();
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
}

//
// Check for device interrupts (cause 6) and handle them.
// Returns 2 for timer interrupt, 1 for other device, 0 if not recognised.
//
int
devintr(void)
{
  uint32 cause = r_cause();

  if(cause != CAUSE_IRQ)
    return 0;

  uint32 pending = mmio_r(IC_PENDING);
  int ret = 0;

  // Handle ALL pending sources — not just the first one.
  // The timer fires very frequently and would starve UART if checked
  // exclusively via else-if.
  if(pending & IC_BIT_UART_RX) {
    uartintr();
    mmio_w(IC_ACK, IC_BIT_UART_RX);
    ret = 1;
  }
  if(pending & IC_BIT_UART_TX) {
    uartintr();
    mmio_w(IC_ACK, IC_BIT_UART_TX);
    ret = 1;
  }
  if(pending & IC_BIT_BLKDEV) {
    virtio_disk_intr();
    mmio_w(IC_ACK, IC_BIT_BLKDEV);
    ret = 1;
  }
  if(pending & IC_BIT_TIMER) {
    clockintr();
    ret = 2;  // timer last so ret=2 triggers yield
  }

  return ret ? ret : 1;
}
