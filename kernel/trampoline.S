// CPUTwo user/kernel trap trampoline.
//
// This page is mapped at TRAMPOLINE (0x03EFE000) in both the kernel and
// every user page table, so the code executes correctly after any page
// table switch.
//
// Trapframe layout (must match struct trapframe in proc.h):
//   +0   kernel_satp
//   +4   kernel_sp
//   +8   kernel_trap  (address of usertrap())
//   +12  epc          (user PC; written here from REG_EPC)
//   +16  kernel_hartid
//   +20  r0 .. r14   (user registers; 15 words = 60 bytes)
//
// CPUTwo trap notes:
//   - On any exception/interrupt from user mode, hardware sets supervisor mode,
//     clears STATUS.IE, saves PC+4 (SYSCALL) or faulting PC (faults/IRQ)
//     into REG_EPC, saves flags into REG_EFLAGS, sets CAUSE, and jumps to
//     EVEC[cause * 4].
//   - r14 (lr) is destroyed by hardware during the vector dispatch.
//     We use it as our base-pointer scratch register inside uservec.
//   - SYSRET: hardware restores PC = EPC, flags = EFLAGS,
//     STATUS = ESTATUS & ~1 (user mode, IE from ESTATUS).
//   - The MMIO region (>= 0x03F00000) always bypasses the MMU, so we can
//     read/write supervisor registers even with the user page table active.
//   - Writing SATP implicitly flushes all non-global TLB entries.

// Assembler constants (must match cputwo.h / memlayout.h)
// REG_EPC     = 0x03FFF000 -> movi r, 0xF000 / movhi r, 0x03FF
// REG_SATP    = 0x03FFF018 -> movi r, 0xF018 / movhi r, 0x03FF
// TRAPFRAME   = 0x03EFD000 -> movi r, 0xD000 / movhi r, 0x03EF

// Place trampoline on a fresh page boundary so we can map it at TRAMPOLINE.
.section .text
.align 4096
.global trampoline_start
trampoline_start:
.global uservec
uservec:
        // ---------------------------------------------------------------
        // Entry: user mode just trapped; we are now in supervisor mode.
        // r14 (lr) has been clobbered by hardware during vector dispatch.
        // Use r14 as a scratch base register pointing at TRAPFRAME.
        // ---------------------------------------------------------------

        // Load TRAPFRAME = 0x03EFD000 into r14.
        movi  lr, 0xD000             // lr = 0x0000D000
        movhi lr, 0x03EF             // lr = 0x03EFD000 = TRAPFRAME

        // Save user registers r0-r13 (r14 is clobbered, will save 0 below).
        sw r0,  20(lr)
        sw r1,  24(lr)
        sw r2,  28(lr)
        sw r3,  32(lr)
        sw r4,  36(lr)
        sw r5,  40(lr)
        sw r6,  44(lr)
        sw r7,  48(lr)
        sw r8,  52(lr)
        sw r9,  56(lr)
        sw r10, 60(lr)
        sw r11, 64(lr)
        sw r12, 68(lr)
        sw r13, 72(lr)   // user sp

        // r14 was clobbered by hardware; save 0 as a placeholder.
        movi  r0, 0
        sw    r0, 76(lr)

        // Read REG_EPC (user return address) and save to trapframe->epc.
        // REG_EPC = 0x03FFF000
        movi  r0, 0xF000
        movhi r0, 0x03FF             // r0 = 0x03FFF000 = REG_EPC
        lw    r0, 0(r0)              // r0 = *REG_EPC
        sw    r0, 12(lr)             // trapframe->epc = user PC

        // Switch to the kernel stack.
        lw    r13, 4(lr)             // sp = trapframe->kernel_sp

        // Restore kernel hartid into r11.
        lw    r11, 16(lr)            // r11 = trapframe->kernel_hartid

        // Load address of usertrap() into r0.
        lw    r0, 8(lr)              // r0 = trapframe->kernel_trap = usertrap

        // Load kernel SATP into r1.
        lw    r1, 0(lr)              // r1 = trapframe->kernel_satp

        // Switch to kernel page table.
        // REG_SATP = 0x03FFF018 (MMIO; always bypasses translation).
        movi  r2, 0xF018
        movhi r2, 0x03FF             // r2 = 0x03FFF018 = REG_SATP
        sw    r1, 0(r2)              // *REG_SATP = kernel SATP (flushes TLB)

        // Call usertrap().  callr lr, r0  →  lr = PC+4, PC = r0.
        // usertrap() never returns to this call site; it calls usertrapret()
        // which calls userret below.
        callr lr, r0

        // Should never reach here.
.global uservec_end
uservec_end:

// ---------------------------------------------------------------------------
// userret(uint32 satp)
//
// Called from usertrapret() in trap.c with r0 = user SATP value.
// Restores user registers from TRAPFRAME and executes SYSRET.
// ---------------------------------------------------------------------------
.global userret
userret:
        // r0 = new user SATP value (first argument from usertrapret).

        // Load TRAPFRAME address into r1 (r1 will be restored from trapframe).
        movi  r1, 0xD000
        movhi r1, 0x03EF             // r1 = 0x03EFD000 = TRAPFRAME

        // Switch to user page table.
        // REG_SATP = 0x03FFF018 — use r2 as temp (r2 will be restored).
        movi  r2, 0xF018
        movhi r2, 0x03FF             // r2 = 0x03FFF018 = REG_SATP
        sw    r0, 0(r2)              // *REG_SATP = user SATP (flushes TLB)

        // TRAPFRAME is mapped in the user page table, so we can still read it.
        // Restore user registers.  Restore r0, r2-r13 first, then r1 last
        // (r1 is our base pointer — destroying it is safe as the last step).
        lw    r0,  20(r1)            // restore r0
        // skip r1 (base pointer, restored last)
        lw    r2,  28(r1)            // restore r2
        lw    r3,  32(r1)
        lw    r4,  36(r1)
        lw    r5,  40(r1)
        lw    r6,  44(r1)
        lw    r7,  48(r1)
        lw    r8,  52(r1)
        lw    r9,  56(r1)
        lw    r10, 60(r1)
        lw    r11, 64(r1)
        lw    r12, 68(r1)
        lw    r13, 72(r1)            // restore user sp
        // r14 (lr) is clobbered by hardware anyway; restore 0 (placeholder).
        lw    lr,  76(r1)

        // Restore r1 last (destroys our base pointer; safe since SYSRET is next).
        lw    r1,  24(r1)

        // Return to user mode.
        // Hardware: PC = REG_EPC, flags = REG_EFLAGS, STATUS = ESTATUS & ~1.
        sysret
