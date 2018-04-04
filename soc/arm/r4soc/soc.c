/*
 * Copyright (c) 2013-2015 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <device.h>
#include <init.h>

#include <arch/cpu.h>
/**
 *
 * @brief Perform basic hardware initialization
 *
 * @return 0
 */

static int r4soc_init(struct device *arg)
{
	ARG_UNUSED(arg);

	/* Install default handler that simply resets the CPU
	 * if configured in the kernel, NOP otherwise
	 */
	NMI_INIT();
	return 0;
}

SYS_INIT(r4soc_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
