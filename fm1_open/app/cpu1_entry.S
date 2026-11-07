/* SPDX-License-Identifier: MPL-2.0
 * The second core's (CPU1's) entry, after felucca-bsp's chip/fm1_cpu1.S (Kerem Kilic / Melodee, Leo Kuroshita; MPL-2.0; written against
 * the public JieLi AC79 SDK, Apache-2.0). Interrupts off, no interrupt configuration, its own user and supervisor stacks, then out of
 * the start-up interrupt into cpu1_main (cpu1_spike.c, in RAM), which never returns. cpu1_start() puts this address in the word the
 * chip starts CPU1 from. The first test of the second core: see PLAN_DUALCORE_CLOCK.md. */
#define CPU1_USTACK_BYTES 1536	/* the jobs' stack; a multiple of 8 (cpu1_spike.c reads the same number) */
	.section .volatile_ram_code,"ax",@progbits	/* in RAM: CPU1 must not fetch its first instructions from flash */
	.balign 4
	.globl cpu1_entry
cpu1_entry:
	cli
	icfg = 0
	usp = cpu1_ustack_top
	sp = cpu1_sstack_top
	ssp = cpu1_sstack_top
	reti = cpu1_main
	rti

	.section .bss.cpu1_stack,"aw",@nobits
	.balign 8
	.globl cpu1_ustack
cpu1_ustack:		/* after rti CPU1 runs on this one */
	.space CPU1_USTACK_BYTES
cpu1_ustack_top:
	.space 256		/* exceptions only (no interrupts on this core) */
	.globl cpu1_sstack_top
cpu1_sstack_top:
