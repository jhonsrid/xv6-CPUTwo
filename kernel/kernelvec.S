// CPUTwo kernel trap vector.
//
// On CPUTwo, any exception/interrupt while the CPU is in supervisor mode
// triggers an immediate halt (double-fault rule).  However, we keep
// kernelvec as a valid entry point for two purposes:
//   1. Exceptions from early boot (before user processes) should panic, not
//      silently halt without a message.  By pointing EVEC entries here and
//      having kernelvec print a message before halting, we get useful output.
//   2. It satisfies the C forward declarations expected by trap.c and main.c.
//
// NOTE: In practice, this code is unreachable once the OS is running with
// interrupts disabled in supervisor mode.  The double-fault rule means the
// CPU halts before kernelvec can execute.

.global kernelvec
kernelvec:
        // Save all caller-saved registers so kerneltrap() can print them.
        // Use a hard-coded stack area just below MMIO (0x03EFFF00).
        // This is the emergency stack; the normal kernel stack is up
        // only after start() is called.
        movi  r13, 0xFF00
        movhi r13, 0x03EF            // sp = 0x03EFFF00 (emergency stack)

        // Make room for 15 registers (r0-r14, except sp which we just set).
        addi  r13, r13, -60

        sw r0,  0(r13)
        sw r1,  4(r13)
        sw r2,  8(r13)
        sw r3, 12(r13)
        sw r4, 16(r13)
        sw r5, 20(r13)
        sw r6, 24(r13)
        sw r7, 28(r13)
        sw r8, 32(r13)
        sw r9, 36(r13)
        sw r10, 40(r13)
        sw r11, 44(r13)
        sw r12, 48(r13)
        sw lr,  52(r13)

        jmp   lr, kerneltrap         // call kerneltrap() to print panic info

        // kerneltrap() will halt via panic() or halt instruction.
        // If it somehow returns, loop forever.
1:
        ba    1b
