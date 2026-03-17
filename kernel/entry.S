// CPUTwo kernel entry point.
//
// The emulator loads the kernel ELF and starts at the ELF entry point
// (_entry).  We don't need a physical reset vector at 0x0; the ELF entry
// point can be anywhere.  The kernel is linked at 0x1000.

.section .text
.global _entry
_entry:
        // Hart ID: CPUTwo has no hartid register.  Use 0 (single-CPU).
        // Keep hart ID in r11 (used as tp by the kernel).
        movi  r11, 0

        // Set up an initial supervisor stack at 0x03EFFFFC —
        // one word below the MMIO base, safe to use before start() runs.
        // Use movi+movhi to load the 32-bit constant into sp (r13).
        movi  r13, 0xFFFC       // r13 = 0x0000FFFC
        movhi r13, 0x03EF       // r13 = 0x03EFFFFC

        // Call start() in start.c (PC-relative branch through lr).
        jmp   lr, start

spin:
        ba    spin              // should never reach here
