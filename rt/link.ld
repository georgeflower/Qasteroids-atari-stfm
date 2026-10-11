OUTPUT_FORMAT("elf32-m68k")
ENTRY(_start)
SECTIONS {
  . = 0;
  .text : { *(.text.start) *(.text .text.*) . = ALIGN(4); }
  .data : { *(.rodata .rodata.*) *(.data .data.*) . = ALIGN(4); }
  .bss  : { *(.bss .bss.* COMMON) . = ALIGN(4); }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gnu*) }
}
