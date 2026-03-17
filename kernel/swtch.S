// Context switch for CPUTwo.
//
// void swtch(struct context *old, struct context *new);
//
// CPUTwo calling convention: r0 = first arg (old), r1 = second arg (new).
//
// Saves callee-saved registers in 'old', loads from 'new'.
// Context layout (must match struct context in proc.h):
//   +0   lr   (r14 — return address)
//   +4   sp   (r13 — stack pointer)
//   +8   r4
//   +12  r5
//   +16  r6
//   +20  r7
//   +24  r8
//   +28  r9
//   +32  r10
//   +36  r11  (hartid / tp equivalent)

.global swtch
swtch:
        // Save current context into *old (r0).
        sw lr,   0(r0)
        sw r13,  4(r0)
        sw r4,   8(r0)
        sw r5,  12(r0)
        sw r6,  16(r0)
        sw r7,  20(r0)
        sw r8,  24(r0)
        sw r9,  28(r0)
        sw r10, 32(r0)
        sw r11, 36(r0)

        // Load new context from *new (r1).
        lw lr,   0(r1)
        lw r13,  4(r1)
        lw r4,   8(r1)
        lw r5,  12(r1)
        lw r6,  16(r1)
        lw r7,  20(r1)
        lw r8,  24(r1)
        lw r9,  28(r1)
        lw r10, 32(r1)
        lw r11, 36(r1)

        // Return to wherever the new context was suspended.
        mov r15, lr
