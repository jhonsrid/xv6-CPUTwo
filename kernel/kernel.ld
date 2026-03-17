OUTPUT_ARCH( "cputwo" )
ENTRY( _entry )

SECTIONS
{
  /*
   * Place the reset vector at address 0 — a single JMP to _entry at 0x1000.
   */
  . = 0x00000000;
  .text.reset : {
    *(.text.reset)
  }

  /*
   * Kernel text starts at 0x00001000.
   * The first page (0x0000–0x0FFF) contains only the reset vector;
   * pages 1+ are the real kernel, keeping page 0 as a null-pointer trap.
   */
  . = 0x00001000;

  .text : {
    kernel/entry.o(.text)
    *(.text .text.*)
    . = ALIGN(0x1000);
    _trampoline = .;
    *(trampsec)
    . = ALIGN(0x1000);
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
    PROVIDE(etext = .);
  }

  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*)
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*)
    . = ALIGN(16);
    *(.data .data.*)
  }

  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*)
    . = ALIGN(16);
    *(.bss .bss.*)
  }

  PROVIDE(end = .);
}
