	.thumb

	.section .vectors, "ax"
	.align 2
	.global _vectors
_vectors:
	.word _stack_top        /* initial stack pointer */
	.word _reset + 1        /* reset */
	/* CPU exceptions (NMI through SysTick) */
	.rept 14
	.word _isr_default + 1
	.endr
	/* IRQ 0-51 (RP2350 has 52 external IRQs) */
	.rept 52
	.word _isr_default + 1
	.endr

	.thumb_func
	.global _isr_default
_isr_default:
	b .

	.thumb_func
	.global _reset
_reset:
	/* Copy .data from flash to SRAM */
	ldr  r0, =_data_load
	ldr  r1, =_data_start
	ldr  r2, =_data_end
1:	cmp  r1, r2
	bge  2f
	ldm  r0!, {r3}
	stm  r1!, {r3}
	b    1b

	/* Zero .bss */
2:	ldr  r1, =_bss_start
	ldr  r2, =_bss_end
	movs r3, #0
3:	cmp  r1, r2
	bge  4f
	stm  r1!, {r3}
	b    3b

	/* Point VTOR at our flash vector table so exceptions land here */
4:	ldr  r0, =_vectors
	ldr  r1, =0xE000ED08
	str  r0, [r1]

	bl   main
	b    .
