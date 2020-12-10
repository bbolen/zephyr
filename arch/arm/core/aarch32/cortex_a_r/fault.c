/*
 * Copyright (c) 2020 Stephanos Ioannidis <root@stephanos.io>
 * Copyright (c) 2018 Lexmark International, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <kernel_internal.h>
#include <logging/log.h>
LOG_MODULE_DECLARE(os);

#define FAULT_DUMP_VERBOSE	(CONFIG_FAULT_DUMP == 2)

#if FAULT_DUMP_VERBOSE
static const char *get_dbgdscr_moe_string(u32_t moe)
{
	switch (moe) {
	case DBGDSCR_MOE_HALT_REQUEST:
		return "Halt Request";
	case DBGDSCR_MOE_BREAKPOINT:
		return "Breakpoint";
	case DBGDSCR_MOE_ASYNC_WATCHPOINT:
		return "Asynchronous Watchpoint";
	case DBGDSCR_MOE_BKPT_INSTRUCTION:
		return "BKPT Instruction";
	case DBGDSCR_MOE_EXT_DEBUG_REQUEST:
		return "External Debug Request";
	case DBGDSCR_MOE_VECTOR_CATCH:
		return "Vector Catch";
	case DBGDSCR_MOE_OS_UNLOCK_CATCH:
		return "OS Unlock Catch";
	case DBGDSCR_MOE_SYNC_WATCHPOINT:
		return "Synchronous Watchpoint";
	default:
		return "Unknown";
	}
}

static void dump_debug_event(void)
{
	/* Read and parse debug mode of entry */
	u32_t dbgdscr = __get_DBGDSCR();
	u32_t moe = (dbgdscr & DBGDSCR_MOE_Msk) >> DBGDSCR_MOE_Pos;

	/* Print debug event information */
	LOG_ERR("Debug Event (%s)", get_dbgdscr_moe_string(moe));
}

static void dump_fault(u32_t status, u32_t addr)
{
	/*
	 * Dump fault status and, if applicable, tatus-specific information.
	 * Note that the fault address is only displayed for the synchronous
	 * faults because it is unpredictable for asynchronous faults.
	 */
	switch (status) {
	case FSR_FS_ALIGNMENT_FAULT:
		LOG_ERR("Alignment Fault @ 0x%08x", addr);
		break;
	case FSR_FS_BACKGROUND_FAULT:
		LOG_ERR("Background Fault @ 0x%08x", addr);
		break;
	case FSR_FS_PERMISSION_FAULT:
		LOG_ERR("Permission Fault @ 0x%08x", addr);
		break;
	case FSR_FS_SYNC_EXTERNAL_ABORT:
		LOG_ERR("Synchronous External Abort @ 0x%08x", addr);
		break;
	case FSR_FS_ASYNC_EXTERNAL_ABORT:
		LOG_ERR("Asynchronous External Abort");
		break;
	case FSR_FS_SYNC_PARITY_ERROR:
		LOG_ERR("Synchronous Parity/ECC Error @ 0x%08x", addr);
		break;
	case FSR_FS_ASYNC_PARITY_ERROR:
		LOG_ERR("Asynchronous Parity/ECC Error");
		break;
	case FSR_FS_DEBUG_EVENT:
		dump_debug_event();
		break;
	default:
		LOG_ERR("Unknown (%u)", status);
	}
}
#endif

static void dump_callee_saved_registers(const _callee_saved_t *cs)
{
	LOG_ERR(" r4: 0x%08x  r5:  0x%08x  r6:  %08x",
			cs->v1, cs->v2, cs->v3);
	LOG_ERR(" r7: 0x%08x  r8:  0x%08x  r9:  %08x",
			cs->v4, cs->v5, cs->v6);
	LOG_ERR("r10: 0x%08x r11:  0x%08x psp:  %08x",
			cs->v7, cs->v8, cs->psp);
}


/**
 * @brief Undefined instruction fault handler
 *
 * @return Returns true if the fault is fatal
 */
#if defined(CONFIG_FPU_SHARING)
bool z_arm_fault_undef_instruction_fp(z_arch_esf_t *esf)
{
	/*
	 * Assume this is a floating point instruction that faulted because
	 * the FP unit was disabled.  Enable the FP unit and try again.  If
	 * the FP was already enabled then this was an actual undefined
	 * instruction.
	 */
	if (__get_FPEXC() & FPEXC_EN) {
		return true;
	}

	__set_FPEXC(FPEXC_EN);

	if (_kernel.cpus[0].nested > 1) {
		/*
		 * If the nested count is greater than 1, the undefined instruction
		 * exception came from an irq/svc context.  (The irq/svc handler
		 * would have the nested count at 1 and then the undef exception
		 * would increment it to 2).
		 */
		z_arch_esf_t *spill_esf = (z_arch_esf_t *)_kernel.cpus[0].fp_ctx;

		_kernel.cpus[0].fp_ctx = NULL;

		/*
		 * If the nested count is 2 and the current thread has used the VFP
		 * (whether or not it was actually using the VFP before the current
		 * exception) OR if the nested count is greater than 2 and the VFP
		 * was enabled on the irq/svc entrance for the saved exception stack
		 * frame, then save the floating point context because it is about
		 * to be overwritten.
		 */
		if (((_kernel.cpus[0].nested == 2)
					&& (_current->base.user_options & K_FP_REGS))
				|| ((_kernel.cpus[0].nested > 2)
					&& (spill_esf->undefined & FPEXC_EN))) {
			/* Spill VFP registers to specified exception stack frame */
			spill_esf->undefined |= FPEXC_EN;
			spill_esf->fpscr = __get_FPSCR();
			__asm__ volatile (
				"vstmia %0, {s0-s15};\n"
				: : "r" (&spill_esf->s[0])
				: "memory"
				);
		}
	} else {
		/*
		 * If the nested count is one, a thread was the faulting
		 * context.  Just flag that this thread uses the VFP.  This
		 * means that a thread that uses the VFP does not have to,
		 * but should, set K_FP_REGS on thread creation.
		 */
		_current->base.user_options |= K_FP_REGS;
	}

	return false;
}
#endif

bool z_arm_fault_undef_instruction(z_arch_esf_t *esf,
		const _callee_saved_t *exc_cs)
{
#if defined(CONFIG_FPU_SHARING)
	/*
	 * This is a true undefined instruction and we will be crashing
	 * so save away the VFP registers.
	 */
	esf->undefined = __get_FPEXC();
	esf->fpscr = __get_FPSCR();
	__asm__ volatile (
		"vstmia %0, {s0-s15};\n"
		: : "r" (&esf->s[0])
		: "memory"
		);
#endif

	/* Print fault information */
	LOG_ERR("***** UNDEFINED INSTRUCTION ABORT *****");

	dump_callee_saved_registers(exc_cs);

	/* Invoke kernel fatal exception handler */
	z_arm_fatal_error(K_ERR_CPU_EXCEPTION, esf);

	/* All undefined instructions are treated as fatal for now */
	return true;
}

/**
 * @brief Prefetch abort fault handler
 *
 * @return Returns true if the fault is fatal
 */
bool z_arm_fault_prefetch(z_arch_esf_t *esf, const _callee_saved_t *exc_cs)
{
	/* Read and parse Instruction Fault Status Register (IFSR) */
	u32_t ifsr = __get_IFSR();
	u32_t fs = ((ifsr & IFSR_FS1_Msk) >> 6) | (ifsr & IFSR_FS0_Msk);

	/* Read Instruction Fault Address Register (IFAR) */
	u32_t ifar = __get_IFAR();

	/* Print fault information*/
	LOG_ERR("***** PREFETCH ABORT *****");
	if (FAULT_DUMP_VERBOSE) {
		dump_fault(fs, ifar);
	}

	dump_callee_saved_registers(exc_cs);

	/* Invoke kernel fatal exception handler */
	z_arm_fatal_error(K_ERR_CPU_EXCEPTION, esf);

	/* All prefetch aborts are treated as fatal for now */
	return true;
}

/**
 * @brief Data abort fault handler
 *
 * @return Returns true if the fault is fatal
 */
bool z_arm_fault_data(z_arch_esf_t *esf, const _callee_saved_t *exc_cs)
{
	/* Read and parse Data Fault Status Register (DFSR) */
	u32_t dfsr = __get_DFSR();
	u32_t fs = ((dfsr & DFSR_FS1_Msk) >> 6) | (dfsr & DFSR_FS0_Msk);

	/* Read Data Fault Address Register (DFAR) */
	u32_t dfar = __get_DFAR();

	/* Print fault information*/
	LOG_ERR("***** DATA ABORT *****");
	if (FAULT_DUMP_VERBOSE) {
		dump_fault(fs, dfar);
	}

	dump_callee_saved_registers(exc_cs);

	/* Invoke kernel fatal exception handler */
	z_arm_fatal_error(K_ERR_CPU_EXCEPTION, esf);

	/* All data aborts are treated as fatal for now */
	return true;
}

/**
 * @brief Initialisation of fault handling
 */
void z_arm_fault_init(void)
{
	/* Nothing to do for now */
}
