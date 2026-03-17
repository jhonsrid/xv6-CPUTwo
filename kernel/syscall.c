#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "cputwo.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// CPUTwo syscall ABI:
//   r0 = syscall number
//   r1 = arg0, r2 = arg1, r3 = arg2  (up to 3 register args; more on stack)
//   r0 = return value

// Fetch the uint32 at addr from the current process.
int
fetchaddr(uint32 addr, uint32 *ip)
{
  struct proc *p = myproc();
  if(addr >= p->sz || addr+sizeof(uint32) > p->sz)
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the NUL-terminated string at addr from the current process.
// Returns length of string (not including NUL), or -1 on error.
int
fetchstr(uint32 addr, char *buf, int max)
{
  struct proc *p = myproc();
  if(copyinstr(p->pagetable, buf, addr, max) < 0)
    return -1;
  return strlen(buf);
}

static uint32
argraw(int n)
{
  struct proc *p = myproc();
  switch(n){
  case 0: return p->trapframe->r1;   // first arg in r1
  case 1: return p->trapframe->r2;
  case 2: return p->trapframe->r3;
  }
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
void
argint(int n, int *ip)
{
  *ip = argraw(n);
}

// Retrieve an argument as a pointer.
void
argaddr(int n, uint32 *ip)
{
  *ip = argraw(n);
}

// Fetch the nth syscall argument as a NUL-terminated string.
// Copies into buf (at most max bytes).
// Returns string length (including NUL) if OK, -1 on error.
int
argstr(int n, char *buf, int max)
{
  uint32 addr;
  argaddr(n, &addr);
  return fetchstr(addr, buf, max);
}

// Prototypes for all syscall handler functions.
extern uint32 sys_fork(void);
extern uint32 sys_exit(void);
extern uint32 sys_wait(void);
extern uint32 sys_pipe(void);
extern uint32 sys_read(void);
extern uint32 sys_kill(void);
extern uint32 sys_exec(void);
extern uint32 sys_fstat(void);
extern uint32 sys_chdir(void);
extern uint32 sys_dup(void);
extern uint32 sys_getpid(void);
extern uint32 sys_sbrk(void);
extern uint32 sys_pause(void);
extern uint32 sys_uptime(void);
extern uint32 sys_open(void);
extern uint32 sys_write(void);
extern uint32 sys_mknod(void);
extern uint32 sys_unlink(void);
extern uint32 sys_link(void);
extern uint32 sys_mkdir(void);
extern uint32 sys_close(void);

static uint32 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_pause]   sys_pause,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
};

void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  // Syscall number is in r0.
  num = (int)p->trapframe->r0;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // Call the handler; store return value in r0 (first return register).
    p->trapframe->r0 = syscalls[num]();
  } else {
    printf("%d %s: unknown sys call %d\n", p->pid, p->name, num);
    p->trapframe->r0 = (uint32)-1;
  }
}
