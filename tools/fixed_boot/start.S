/* Minimal non-secure/non-encrypted SF32LB52 NOR boot handoff.
 * ROM copies this immutable image from 0x12008000 to 0x20020000.
 * No clock, Flash, PSRAM, calibration or BLE configuration is changed.
 */
.syntax unified
.cpu cortex-m33
.thumb
.section .vectors,"a",%progbits
.global vectors
vectors:
  .word 0x20021000
  .word boot_start
  .rept 14
  .word boot_invalid
  .endr

.section .text.boot,"ax",%progbits
.global boot_start
.type boot_start,%function
.thumb_func
boot_start:
  cpsid i
  ldr r0, =0x12010000
  ldr r1, [r0]
  ldr r2, [r0, #4]
  /* Reject erased images, unaligned/out-of-range stacks, non-Thumb entries. */
  tst r1, #7
  bne boot_invalid
  ldr r3, =0x20000000
  cmp r1, r3
  bls boot_invalid
  ldr r3, =0x2007fc00
  cmp r1, r3
  bhi boot_invalid
  tst r2, #1
  beq boot_invalid
  cmp r2, r0
  blo boot_invalid
  ldr r3, =0x129a0000
  cmp r2, r3
  bhs boot_invalid
  /* Disable SysTick; the application initializes its own interrupts. */
  movs r3, #0
  ldr r4, =0xe000e010
  str r3, [r4]
  /* Remove stack limits left by a preceding stage, select privileged MSP. */
  msr basepri, r3
  msr faultmask, r3
  msr msplim, r3
  msr psplim, r3
  msr control, r3
  isb
  ldr r4, =0xe000ed08
  str r0, [r4]
  dsb
  isb
  msr msp, r1
  mov lr, r3
  bx r2
.size boot_start, .-boot_start

.global boot_invalid
.type boot_invalid,%function
.thumb_func
boot_invalid:
  cpsid i
1: wfi
  b 1b
.size boot_invalid, .-boot_invalid
.ltorg
