/*
 * Copyright (c) 2020 Stephanos Ioannidis <root@stephanos.io>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Exception handlers for ARM Cortex-A and Cortex-R
 *
 * This file implements the exception handlers (undefined instruction, prefetch
 * abort and data abort) for ARM Cortex-A and Cortex-R processors.
 *
 * All exception handlers save the exception stack frame into the exception
 * mode stack rather than the system mode stack, in order to ensure predictable
 * exception behaviour (i.e. an arbitrary thread stack overflow cannot cause
 * exception handling and thereby subsequent total system failure).
 *
 * In case the exception is due to a fatal (unrecoverable) fault, the fault
 * handler is responsible for invoking the architecture fatal exception handler
 * (z_arm_fatal_error) which invokes the kernel fatal exception handler
 * (z_fatal_error) that either locks up the system or aborts the current thread
 * depending on the application exception handler implementation.
 */

#include <toolchain.h>
#include <linker/sections.h>
#include <offsets_short.h>
#include <arch/cpu.h>

_ASM_FILE_PROLOGUE

GTEXT(z_arm_fault_undef_instruction)
#if defined(CONFIG_FPU_SHARING)
GTEXT(z_arm_fault_undef_instruction_fp)
#endif
GTEXT(z_arm_fault_prefetch)
GTEXT(z_arm_fault_data)

GTEXT(z_arm_undef_instruction)
GTEXT(z_arm_prefetch_abort)
GTEXT(z_arm_data_abort)

/**
 * @brief Undefined instruction exception handler
 *
 * An undefined instruction (UNDEF) exception is generated when an undefined
 * instruction, or a VFP instruction when the VFP is not enabled, is
 * encountered.
 */
SECTION_SUBSEC_FUNC(TEXT, __exc, z_arm_undef_instruction)
	/*
	 * The undefined instruction address is offset by 2 if the previous
	 * mode is Thumb; otherwise, it is offset by 4.
	 */
	push {r0}
	mrs r0, spsr
	tst r0, #T_BIT
	subeq lr, #4	/* ARM   (!T_BIT) */
	subne lr, #2	/* Thumb (T_BIT) */
	pop {r0}

	/*
	 * Store r0-r3, r12, lr, lr_und and spsr_und into the stack to
	 * construct an exception stack frame.
	 */
	srsdb sp!, #MODE_UND
#if defined(CONFIG_FPU_SHARING)
	sub sp, #FPU_FRAME_SIZE
#endif
	stmfd sp, {r0-r3, r12, lr}^
	sub sp, #24

	/* Increment exception nesting count */
	ldr r2, =_kernel
	ldr r0, [r2, #_kernel_offset_to_nested]
	add r0, r0, #1
	str r0, [r2, #_kernel_offset_to_nested]

	/* save esf pointer */
	mov r0, sp

#if defined(CONFIG_FPU_SHARING)
	bl z_arm_fault_undef_instruction_fp
	cmp r0, #0
	beq z_arm_exc_exit

	/* Restore r0 to the esf pointer for the next undef handler */
	mov r0, sp
#endif

	sub r1, sp, #36
	cps #MODE_SYS
	stm r1, {r4-r11, sp}
	cps #MODE_UND
	mov sp, r1

	/* Invoke fault handler */
	bl z_arm_fault_undef_instruction

	/* Exit exception */
	add sp, sp, #36
	b z_arm_exc_exit

/**
 * @brief Prefetch abort exception handler
 *
 * A prefetch abort (PABT) exception is generated when the processor marks the
 * prefetched instruction as invalid and the instruction is executed.
 */
SECTION_SUBSEC_FUNC(TEXT, __exc, z_arm_prefetch_abort)
	/*
	 * The faulting instruction address is always offset by 4 for the
	 * prefetch abort exceptions.
	 */
	sub lr, #4

	/*
	 * Store r0-r3, r12, lr, lr_abt and spsr_abt into the stack to
	 * construct an exception stack frame.
	 */
	srsdb sp!, #MODE_ABT
#if defined(CONFIG_FPU_SHARING)
	sub sp, #FPU_FRAME_SIZE
#endif
	stmfd sp, {r0-r3, r12, lr}^
	sub sp, #24

#if defined(CONFIG_FPU_SHARING)
	vmrs r1, fpexc
	mov r0, #FPEXC_EN
	vmsr fpexc, r0
	vmrs r0, fpscr

	add r2, sp, #24
	vstmia r2!, {s0-s15}
	stm r2, {r0, r1}
#endif

	/* Increment exception nesting count */
	ldr r2, =_kernel
	ldr r0, [r2, #_kernel_offset_to_nested]
	add r0, r0, #1
	str r0, [r2, #_kernel_offset_to_nested]

	/* save esf pointer */
	mov r0, sp

	sub r1, sp, #36
	cps #MODE_SYS
	stm r1, {r4-r11, sp}
	cps #MODE_ABT
	mov sp, r1

	/* Invoke fault handler */
	bl z_arm_fault_prefetch

	/* Exit exception */
	add sp, sp, #36
	b z_arm_exc_exit

/**
 * @brief Data abort exception handler
 *
 * A data abort (DABT) exception is generated when an error occurs on a data
 * memory access. This exception can be either synchronous or asynchronous,
 * depending on the type of fault that caused it.
 */
SECTION_SUBSEC_FUNC(TEXT, __exc, z_arm_data_abort)
	/*
	 * The faulting instruction address is always offset by 8 for the data
	 * abort exceptions.
	 */
	sub lr, #8

	/*
	 * Store r0-r3, r12, lr, lr_abt and spsr_abt into the stack to
	 * construct an exception stack frame.
	 */
	srsdb sp!, #MODE_ABT
#if defined(CONFIG_FPU_SHARING)
	sub sp, #FPU_FRAME_SIZE
#endif
	stmfd sp, {r0-r3, r12, lr}^
	sub sp, #24

#if defined(CONFIG_FPU_SHARING)
	vmrs r1, fpexc
	mov r0, #FPEXC_EN
	vmsr fpexc, r0
	vmrs r0, fpscr

	add r2, sp, #24
	vstmia r2!, {s0-s15}
	stm r2, {r0, r1}
#endif

	/* Increment exception nesting count */
	ldr r2, =_kernel
	ldr r0, [r2, #_kernel_offset_to_nested]
	add r0, r0, #1
	str r0, [r2, #_kernel_offset_to_nested]

	/* save esf pointer */
	mov r0, sp

	sub r1, sp, #36
	cps #MODE_SYS
	stm r1, {r4-r11, sp}
	cps #MODE_ABT
	mov sp, r1

	/* Invoke fault handler */
	bl z_arm_fault_data

	/* Exit exception */
	add sp, sp, #36
	b z_arm_exc_exit
