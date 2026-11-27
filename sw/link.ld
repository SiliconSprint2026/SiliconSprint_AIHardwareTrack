/* TENSOR-CORE firmware layout.
 *   ITCM 0x0000_0000  64 KB  code, read-only data, and the initial image of .data
 *   DTCM 0x1000_0000  64 KB  .data, .bss, stack (grows down from 0x1001_0000)
 *        0x1001_0000 192 KB  weight blob, loaded by the harness (see docs/BLOB_FORMAT.md)
 * Loaders place each segment at its load address (LMA), so everything in the ELF lands in
 * ITCM; crt0.S copies .data to DTCM and zeroes .bss.
 */
ENTRY(_start)

MEMORY {
  ITCM (rx)  : ORIGIN = 0x00000000, LENGTH = 64K
  DTCM (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
}

STACK_SIZE = 8K;

SECTIONS {
  .text : {
    KEEP(*(.text.start))
    *(.text .text.*)
  } > ITCM

  .rodata : ALIGN(16) {
    *(.rodata .rodata.* .srodata .srodata.*)
  } > ITCM

  .data : ALIGN(16) {
    __data_start = .;
    *(.data .data.* .sdata .sdata.*)
    . = ALIGN(16);
    __data_end = .;
  } > DTCM AT > ITCM
  __data_load = LOADADDR(.data);

  .bss (NOLOAD) : ALIGN(16) {
    __bss_start = .;
    *(.bss .bss.* .sbss .sbss.* COMMON)
    . = ALIGN(16);
    __bss_end = .;
  } > DTCM

  __stack_top = ORIGIN(DTCM) + LENGTH(DTCM);
  ASSERT(__bss_end + STACK_SIZE <= __stack_top, "DTCM work area full: .data + .bss + stack > 64 KB")
  __global_pointer$ = __data_start + 0x800;
}
