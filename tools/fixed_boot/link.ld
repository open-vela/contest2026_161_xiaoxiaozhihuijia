ENTRY(boot_start)
MEMORY { ram (rwx) : ORIGIN = 0x20020000, LENGTH = 0x1000 }
SECTIONS {
  .vectors : { KEEP(*(.vectors)) } > ram
  .text : { *(.text*) *(.rodata*) } > ram
  /DISCARD/ : { *(.comment) *(.ARM.attributes) }
}
ASSERT(SIZEOF(.vectors) + SIZEOF(.text) <= 0x800, "Fixed loader too large")
