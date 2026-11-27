/* Startup: set sp and gp, copy .data from ITCM to DTCM, zero .bss, run main,
 * then write main's return value to HALT. */
    .section .text.start, "ax"
    .globl _start
_start:
    .option push
    .option norelax
    la    gp, __global_pointer$
    .option pop
    la    sp, __stack_top

    la    t0, __data_load          /* copy .data */
    la    t1, __data_start
    la    t2, __data_end
1:  bgeu  t1, t2, 2f
    lw    t3, 0(t0)
    sw    t3, 0(t1)
    addi  t0, t0, 4
    addi  t1, t1, 4
    j     1b
2:  la    t1, __bss_start          /* zero .bss */
    la    t2, __bss_end
3:  bgeu  t1, t2, 4f
    sw    zero, 0(t1)
    addi  t1, t1, 4
    j     3b
4:  call  main
    li    t0, 0x20000004           /* HALT = exit code */
    sw    a0, 0(t0)
5:  j     5b
