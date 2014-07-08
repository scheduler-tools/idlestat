/*
 *  trace.h
 *
 *  Copyright (C) 2014  Zoran Markovic <zoran.markovic@linaro.org>
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
 */
#ifndef __TRACE_H
#define __TRACE_H

#define TRACE_PATH "/sys/kernel/debug/tracing"
#define TRACE_ON_PATH TRACE_PATH "/tracing_on"
#define TRACE_BUFFER_SIZE_PATH TRACE_PATH "/buffer_size_kb"
#define TRACE_BUFFER_TOTAL_PATH TRACE_PATH "/buffer_total_size_kb"
#define TRACE_CPUIDLE_EVENT_PATH TRACE_PATH "/events/power/cpu_idle/enable"
#define TRACE_CPUFREQ_EVENT_PATH TRACE_PATH "/events/power/cpu_frequency/enable"
#define TRACE_IRQ_EVENT_PATH TRACE_PATH "/events/irq/enable"
#define TRACE_IPI_EVENT_PATH TRACE_PATH "/events/ipi/enable"
#define TRACE_EVENT_PATH TRACE_PATH "/events/enable"
#define TRACE_FREE TRACE_PATH "/free_buffer"
#define TRACE_FILE TRACE_PATH "/trace"
#define TRACE_IDLE_NRHITS_PER_SEC 10000
#define TRACE_IDLE_LENGTH 196
#define TRACE_CPUFREQ_NRHITS_PER_SEC 100
#define TRACE_CPUFREQ_LENGTH 196

extern int idlestat_trace_enable(bool enable);
extern int idlestat_flush_trace(void);
extern int idlestat_init_trace(unsigned int duration);

#endif
