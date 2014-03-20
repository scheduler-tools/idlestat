/*
 *  trace.c
 *
 *  Copyright (C) 2014, Linaro Limited.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Contributors:
 *     Daniel Lezcano <daniel.lezcano@linaro.org>
 *     Zoran Markovic <zoran.markovic@linaro.org>
 *
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

#include "trace.h"
#include "utils.h"

int idlestat_trace_enable(bool enable)
{
	return write_int(TRACE_ON_PATH, enable);
}

int idlestat_flush_trace(void)
{
	return write_int(TRACE_FILE, 0);
}

int idlestat_init_trace(unsigned int duration)
{
	int bufsize;

	/* Assuming the worst case where we can have for cpuidle,
	 * TRACE_IDLE_NRHITS_PER_SEC.  Each state enter/exit line are
	 * 196 chars wide, so we have 2 x 196 x TRACE_IDLE_NRHITS_PER_SEC lines.
	 * For cpufreq, assume a 196-character line for each frequency change,
	 * and expect a rate of TRACE_CPUFREQ_NRHITS_PER_SEC. 
	 * Divide by 2^10 to have Kb. We add 1Kb to be sure to round up.
	*/

	bufsize = 2 * TRACE_IDLE_LENGTH * TRACE_IDLE_NRHITS_PER_SEC;
	bufsize += TRACE_CPUFREQ_LENGTH * TRACE_CPUFREQ_NRHITS_PER_SEC;
	bufsize = (bufsize * duration / (1 << 10)) + 1;

	if (write_int(TRACE_BUFFER_SIZE_PATH, bufsize))
		return -1;

	if (read_int(TRACE_BUFFER_TOTAL_PATH, &bufsize))
		return -1;

	printf("Total trace buffer: %d kB\n", bufsize);

	/* Disable all the traces */
	if (write_int(TRACE_EVENT_PATH, 0))
		return -1;

	/* Enable cpu_idle traces */
	if (write_int(TRACE_CPUIDLE_EVENT_PATH, 1))
		return -1;

	/* Enable cpu_frequency traces */
	if (write_int(TRACE_CPUFREQ_EVENT_PATH, 1))
		return -1;

	/* Enable irq traces */
	if (write_int(TRACE_IRQ_EVENT_PATH, 1))
		return -1;

	return 0;
}
