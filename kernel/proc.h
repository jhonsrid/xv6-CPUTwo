// Saved registers for kernel context switches.
// CPUTwo callee-saved: r4-r10, lr (r14), sp (r13).
// r11 is used as hartid (tp equivalent) and saved too.
struct context {
  uint32 lr;   // return address (r14)
  uint32 sp;   // stack pointer (r13)
  uint32 r4;
  uint32 r5;
  uint32 r6;
  uint32 r7;
  uint32 r8;
  uint32 r9;
  uint32 r10;
  uint32 r11;  // hartid (tp equivalent)
};

// Per-CPU state.
struct cpu {
  struct proc *proc;          // The process running on this cpu, or null.
  struct context context;     // swtch() here to enter scheduler().
  int noff;                   // Depth of push_off() nesting.
  int intena;                 // Were interrupts enabled before push_off()?
};

extern struct cpu cpus[NCPU];

// Per-process trap frame.
// Lives in a page just below the trampoline in the user page table.
// uservec in trampoline.S saves user registers here, then loads kernel
// context (kernel_sp, kernel_hartid, kernel_satp, kernel_trap) from here.
// userret restores user registers from here before SYSRET.
//
// Offsets must match the constants in trampoline.S.
struct trapframe {
  /*  0 */ uint32 kernel_satp;   // kernel page table (SATP register value)
  /*  4 */ uint32 kernel_sp;     // top of process's kernel stack
  /*  8 */ uint32 kernel_trap;   // address of usertrap()
  /* 12 */ uint32 epc;           // saved user program counter (from REG_EPC)
  /* 16 */ uint32 kernel_hartid; // saved kernel hartid (r11 value)
  /* 20 */ uint32 r0;            // user general-purpose registers
  /* 24 */ uint32 r1;
  /* 28 */ uint32 r2;
  /* 32 */ uint32 r3;
  /* 36 */ uint32 r4;
  /* 40 */ uint32 r5;
  /* 44 */ uint32 r6;
  /* 48 */ uint32 r7;
  /* 52 */ uint32 r8;
  /* 56 */ uint32 r9;
  /* 60 */ uint32 r10;
  /* 64 */ uint32 r11;           // user r11 (overwritten by kernel hartid use)
  /* 68 */ uint32 r12;
  /* 72 */ uint32 r13;           // user stack pointer
  /* 76 */ uint32 r14;           // user lr (preserved by hardware)
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;        // Process state
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int pid;                     // Process ID

  // wait_lock must be held when using this:
  struct proc *parent;         // Parent process

  // these are private to the process, so p->lock need not be held.
  uint32 kstack;               // Virtual address of kernel stack
  uint32 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
};
