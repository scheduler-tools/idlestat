
#ifndef __TRACE_H
#define __TRACE_H

#define TRACE_PATH "/sys/kernel/debug/tracing"
#define TRACE_ON_PATH TRACE_PATH "/tracing_on"
#define TRACE_BUFFER_SIZE_PATH TRACE_PATH "/buffer_size_kb"
#define TRACE_BUFFER_TOTAL_PATH TRACE_PATH "/buffer_total_size_kb"
#define TRACE_CPUIDLE_EVENT_PATH TRACE_PATH "/events/power/cpu_idle/enable"
#define TRACE_IRQ_EVENT_PATH TRACE_PATH "/events/irq/enable"
#define TRACE_EVENT_PATH TRACE_PATH "/events/enable"
#define TRACE_FREE TRACE_PATH "/free_buffer"
#define TRACE_FILE TRACE_PATH "/trace"
#define TRACE_IDLE_NRHITS_PER_SEC 10000
#define TRACE_IDLE_LENGTH 196

extern int idlestat_trace_enable(bool enable);
extern int idlestat_flush_trace(void);
extern int idlestat_init_trace(unsigned int duration);

#endif
