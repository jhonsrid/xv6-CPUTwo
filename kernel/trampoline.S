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
//   - On any exception/interrupt, hardware sets supervisor mode,
//     clears STATUS.IE, saves PC+4 (SYSCALL) or faulting PC (faults/IRQ)
//     into REG_EPC, saves flags into REG_EFLAGS, sets CAUSE, and jumps to
//     EVEC[cause * 4].  All GPRs (r0-r14) are preserved by hardware.
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
        // All GPRs hold user values (hardware preserves them).
        // We need a scratch register to build the TRAPFRAME address.
        // Temporarily push user r0 onto the user stack (still mapped).
        // ---------------------------------------------------------------

        addi  r13, r13, -4
        sw    r0, 0(r13)               // [user_sp - 4] = user r0

        // Build TRAPFRAME = 0x03EFD000 in r0 (now free).
        movi  r0, 0xD000
        movhi r0, 0x03EF               // r0 = TRAPFRAME

        // Save user lr (r14) — preserved by hardware, real user value.
        sw    lr, 76(r0)               // trapframe->r14 = user lr

        // Save user r1-r12.
        sw    r1,  24(r0)
        sw    r2,  28(r0)
        sw    r3,  32(r0)
        sw    r4,  36(r0)
        sw    r5,  40(r0)
        sw    r6,  44(r0)
        sw    r7,  48(r0)
        sw    r8,  52(r0)
        sw    r9,  56(r0)
        sw    r10, 60(r0)
        sw    r11, 64(r0)
        sw    r12, 68(r0)

        // Save original user sp (before our push: r13 + 4).
        addi  r1, r13, 4
        sw    r1, 72(r0)               // trapframe->r13 = original user sp

        // Recover user r0 from user stack and save to trapframe.
        lw    r1, 0(r13)               // r1 = user r0 (from user stack)
        sw    r1, 20(r0)               // trapframe->r0 = user r0

        // Switch base to lr for the rest of the sequence.
        mov   lr, r0                   // lr = TRAPFRAME

        // Read REG_EPC (user return address) and save to trapframe->epc.
        // REG_EPC = 0x03FFF000
        movi  r0, 0xF000
        movhi r0, 0x03FF               // r0 = 0x03FFF000 = REG_EPC
        lw    r0, 0(r0)                // r0 = *REG_EPC
        sw    r0, 12(lr)               // trapframe->epc = user PC

        // Switch to the kernel stack.
        lw    r13, 4(lr)               // sp = trapframe->kernel_sp

        // Restore kernel hartid into r11.
        lw    r11, 16(lr)              // r11 = trapframe->kernel_hartid

        // Load address of usertrap() into r0.
        lw    r0, 8(lr)                // r0 = trapframe->kernel_trap = usertrap

        // Load kernel SATP into r1.
        lw    r1, 0(lr)                // r1 = trapframe->kernel_satp

        // Switch to kernel page table.
        // REG_SATP = 0x03FFF018 (MMIO; always bypasses translation).
        movi  r2, 0xF018
        movhi r2, 0x03FF               // r2 = 0x03FFF018 = REG_SATP
        sw    r1, 0(r2)                // *REG_SATP = kernel SATP (flushes TLB)

        // Call usertrap().  callr lr, r0  →  lr = PC+4, PC = r0.
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
        movhi r1, 0x03EF               // r1 = 0x03EFD000 = TRAPFRAME

        // Switch to user page table.
        // REG_SATP = 0x03FFF018 — use r2 as temp (r2 will be restored).
        movi  r2, 0xF018
        movhi r2, 0x03FF               // r2 = 0x03FFF018 = REG_SATP
        sw    r0, 0(r2)                // *REG_SATP = user SATP (flushes TLB)

        // TRAPFRAME is mapped in the user page table, so we can still read it.
        // Restore user registers.  Restore r0, r2-r13 first, then r1 last
        // (r1 is our base pointer — destroying it is safe as the last step).
        lw    r0,  20(r1)              // restore r0
        // skip r1 (base pointer, restored last)
        lw    r2,  28(r1)              // restore r2
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
        lw    r13, 72(r1)              // restore user sp
        lw    lr,  76(r1)              // restore user lr (real value)

        // Restore r1 last (destroys our base pointer; safe since SYSRET is next).
        lw    r1,  24(r1)

        // Return to user mode.
        // Hardware: PC = REG_EPC, flags = REG_EFLAGS, STATUS = ESTATUS & ~1.
        sysret
