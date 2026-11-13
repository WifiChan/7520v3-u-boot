ENTRY(_start)
SECTIONS
{
  . = 0;
  _text = .;

  .text : {
    _start = .;
    KEEP (*(.start))
    KEEP (*(.text))
    *(.text.*)
    *(.fixup)
    *(.gnu.warning)
    *(.glue_7t)
    *(.glue_7)
  }
  .rodata : {
    *(.rodata)
    *(.rodata.*)
  }
  .compressed_data : {
    KEEP (*(.compressed_data))
  }

  . = ALIGN(4);
  _etext = .;
  
  .got.plt		: { *(.got.plt) }
  _got_start = .;
  .got			: { *(.got) }
  _got_end = .;

  /* ensure the zImage file size is always a multiple of 64 bits */
  /* (without a dummy byte, ld just ignores the empty section) */
  .pad			: { BYTE(0); . = ALIGN(8); }
  _edata = .;

  . = ALIGN(8);
  __bss_start = .;
  .bss			: { *(.bss*) }
  __bss_end = .;

  . = ALIGN(8);		/* the stack must be 64-bit aligned */
  .stack		: { *(.stack) }

  .stab 0		: { *(.stab) }
  .stabstr 0		: { *(.stabstr) }
  .stab.excl 0		: { *(.stab.excl) }
  .stab.exclstr 0	: { *(.stab.exclstr) }
  .stab.index 0		: { *(.stab.index) }
  .stab.indexstr 0	: { *(.stab.indexstr) }
  .comment 0		: { *(.comment) }  
}
