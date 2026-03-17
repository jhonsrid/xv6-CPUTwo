// Mutual exclusion spin locks.
// CPUTwo uses the CAS instruction (opcode 0x3D) for atomic test-and-set.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "cputwo.h"
#include "proc.h"
#include "defs.h"

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

// Atomic test-and-set using the CPUTwo CAS instruction.
// CAS semantics: tmp = mem[rs1]; if tmp == rd { mem[rs1] = rs2; Z=1 }
//                                else          { rd = tmp;        Z=0 }
// We want: if *addr == 0, set *addr = 1 and return 0 (old value = unlocked).
//          Otherwise return 1 (was already locked).
// Returns 0 if lock was acquired (old value was 0), 1 if not.
static inline int
cas_lock(volatile int *addr)
{
  int old = 0;      // expected value (unlocked)
  int one = 1;      // desired value (locked)
  int acquired;
  // CAS rd=old, [rs1=addr], rs2=one:
  //   if *addr == old (0): *addr = one, Z=1 → acquired = 1
  //   else:                old = *addr, Z=0 → acquired = 0
  asm volatile(
    "cas %1, %2, %3\n"  // CAS old, [addr], one
    "movi %0, 0\n"      // acquired = 0 (assume failed)
    "beq 1f\n"          // if Z=1 (CAS succeeded), skip
    "ba 2f\n"           // CAS failed: branch to done (acquired stays 0)
    "1: movi %0, 1\n"   // acquired = 1 (CAS succeeded)
    "2:\n"
    : "=r"(acquired), "+r"(old)
    : "r"(addr), "r"(one)
    : "memory"
  );
  return acquired;  // 1 if we got the lock, 0 if not
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
void
acquire(struct spinlock *lk)
{
  push_off(); // disable interrupts to avoid deadlock.
  if(holding(lk))
    panic("acquire");

  // Spin until we successfully set lk->locked from 0 to 1.
  while(cas_lock((volatile int *)&lk->locked) == 0)
    ;

  // Memory barrier: ensure critical section's memory references happen
  // strictly after the lock is acquired.
  __sync_synchronize();

  lk->cpu = mycpu();
}

// Release the lock.
void
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  lk->cpu = 0;

  // Memory barrier: ensure all stores in the critical section are visible
  // before we release the lock.
  __sync_synchronize();

  // Write 0 atomically to release the lock.
  // On CPUTwo, a plain store is sufficient for the release since there
  // are no other CPUs to worry about in single-CPU mode.  The memory
  // barrier above orders the writes.
  lk->locked = 0;

  pop_off();
}

// Check whether this cpu is holding the lock.
// Interrupts must be off.
int
holding(struct spinlock *lk)
{
  int r;
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}

// push_off/pop_off are like intr_off()/intr_on() except that they are matched:
// two pop_off()s undo two push_off()s.

void
push_off(void)
{
  int old = intr_get();
  intr_off();
  if(mycpu()->noff == 0)
    mycpu()->intena = old;
  mycpu()->noff += 1;
}

void
pop_off(void)
{
  struct cpu *c = mycpu();
  if(intr_get())
    panic("pop_off - interruptible");
  if(c->noff < 1)
    panic("pop_off");
  c->noff -= 1;
  if(c->noff == 0 && c->intena)
    intr_on();
}
