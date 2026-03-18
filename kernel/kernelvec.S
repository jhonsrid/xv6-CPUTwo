// CPUTwo kernel-mode trap handler.
//
// Uses KRET (opcode 0x3F) for atomic return: PC=EPC, flags=EFLAGS,
// STATUS=ESTATUS.  This avoids the race where a nested interrupt
// between IE restore and jump could overwrite the return address.
//
// Stack frame (76 bytes = 19 words):
//   sp+ 0..52: r0-r12, lr  (14 regs)
//   sp+56: saved EPC
//   sp+60: saved EFLAGS
//   sp+64: saved ESTATUS

.global kernelvec
kernelvec:
        addi  r13, r13, -68

        // Save all GPRs except sp/pc.
        sw    r0,   0(r13)
        sw    r1,   4(r13)
        sw    r2,   8(r13)
        sw    r3,  12(r13)
        sw    r4,  16(r13)
        sw    r5,  20(r13)
        sw    r6,  24(r13)
        sw    r7,  28(r13)
        sw    r8,  32(r13)
        sw    r9,  36(r13)
        sw    r10, 40(r13)
        sw    r11, 44(r13)
        sw    r12, 48(r13)
        sw    lr,  52(r13)

        // Save supervisor state from MMIO.
        movi  r0, 0xF000
        movhi r0, 0x03FF              // r0 = REG_EPC
        lw    r4, 0(r0)               // r4 = EPC
        lw    r5, 4(r0)               // r5 = EFLAGS
        sw    r4, 56(r13)             // save EPC
        sw    r5, 60(r13)             // save EFLAGS
        movi  r0, 0xF014
        movhi r0, 0x03FF
        lw    r0, 0(r0)               // r0 = ESTATUS
        sw    r0, 64(r13)             // save ESTATUS

        // Call kerneltrap() via EVEC slot 15.
        movi  r0, 0xF008
        movhi r0, 0x03FF
        lw    r0, 0(r0)               // r0 = EVEC table base
        lw    r0, 60(r0)              // r0 = evec_table[15] = &kerneltrap
        callr lr, r0

        // --- Return from kerneltrap ---
        // Write saved EPC, EFLAGS, ESTATUS back to MMIO registers
        // so KRET can restore them atomically.
        movi  r0, 0xF000
        movhi r0, 0x03FF              // r0 = REG_EPC
        lw    r1, 56(r13)             // r1 = saved EPC
        lw    r2, 60(r13)             // r2 = saved EFLAGS
        sw    r1, 0(r0)               // REG_EPC = saved EPC
        sw    r2, 4(r0)               // REG_EFLAGS = saved EFLAGS
        movi  r0, 0xF014
        movhi r0, 0x03FF
        lw    r1, 64(r13)             // r1 = saved ESTATUS
        sw    r1, 0(r0)               // REG_ESTATUS = saved ESTATUS

        // Restore all GPRs.
        lw    r0,   0(r13)
        lw    r1,   4(r13)
        lw    r2,   8(r13)
        lw    r3,  12(r13)
        lw    r4,  16(r13)
        lw    r5,  20(r13)
        lw    r6,  24(r13)
        lw    r7,  28(r13)
        lw    r8,  32(r13)
        lw    r9,  36(r13)
        lw    r10, 40(r13)
        lw    r11, 44(r13)
        lw    r12, 48(r13)
        lw    lr,  52(r13)

        // Restore sp and atomically return.
        // KRET: PC=EPC, flags=EFLAGS, STATUS=ESTATUS (IE restored atomically).
        addi  r13, r13, 68
        .byte 0x00, 0x00, 0x00, 0x3F // KRET (opcode 0x3F, all other bits 0)
