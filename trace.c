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

	/* Assuming the worst case where we can have
	 * TRACE_IDLE_NRHITS_PER_SEC.  Each state enter/exit line are
	 * 196 chars wide, so we have 2 x 196 x TRACE_IDLE_NRHITS_PER_SEC bytes.
	 * divided by 2^10 to have Kb. We add 1Kb to be sure to round up.
	*/

	bufsize = 2 * TRACE_IDLE_LENGTH * TRACE_IDLE_NRHITS_PER_SEC * duration;
	bufsize = (bufsize / (1 << 10)) + 1;

	if (write_int(TRACE_BUFFER_SIZE_PATH, bufsize))
		return -1;

	if (read_int(TRACE_BUFFER_TOTAL_PATH, &bufsize))
		return -1;

	printf("Total trace buffer: %d kB\n", bufsize);

	/* Disable all the traces */
	if (write_int(TRACE_EVENT_PATH, 0))
		return -1;

	/* Enable only cpu_idle traces */
	if (write_int(TRACE_CPUIDLE_EVENT_PATH, 1))
		return -1;

	return 0;
}
