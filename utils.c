/*
 *  utils.c
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
#undef _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

#include "utils.h"
#include "topology.h"

int write_int(const char *path, int val)
{
	FILE *f;

	f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "failed to open '%s': %m\n", path);
		return -1;
	}

	fprintf(f, "%d", val);

	fclose(f);

	return 0;
}

int read_int(const char *path, int *val)
{
	FILE *f;

	f = fopen(path, "r");

	if (!f) {
		fprintf(stderr, "failed to open '%s': %m\n", path);
		return -1;
	}

	fscanf(f, "%d", val);

	fclose(f);

	return 0;
}

#define VEXPRESS_TC2
#ifdef VEXPRESS_TC2

#define TRACE_FORMAT "%*[^]]] %*4s %lf:%*[^=]=%u%*[^=]=%u"
#define TRACE_TIME_FORMAT "%*[^[][%u] %*4s %lf:"

/* Temporary globals, to be better organized */
int cpu_to_cluster[] = {0,0,0,0,0};
#define cluster(CPU) cpu_to_cluster[CPU]
#define cluster_label(CPU) 'A'+cpu_to_cluster[CPU]

/* Keep track of each cluster status  */
struct cluster {
	FILE *gnuplot_freq_fd;
	FILE *gnuplot_idle_fd;
	int cluster_pstate; /* Cluster current P state */
	int cluster_cstate; /* Cluster current C state */
	/* for simplicity, use absolute CPU_ID as index */
	int cpu_valid[5];   /* 1: CPU belong to that cluster */
	int cpu_active[5];  /* 1: CPU currently running */
	int cpu_freq[5];    /* last Freq request by that CPU */
	int cpu_idle[5];    /* last Idle request by that CPU */
} cluster_status[] = {
	{NULL, NULL, 0, 99,
		{ 0,  0,  0,  0,  0}, /* CPU in that cluster */
		{ 0,  0,  0,  0,  0}, /* CPU is active */
		{-1, -1, -1, -1, -1}, /* P-States */
		{-2, -2, -2, -2, -2}, /* C-States */
	}, /* ClusterA, 2x A15 */
	{NULL, NULL, 0, 99,
		{ 0,  0,  0,  0,  0}, /* CPU in that cluster */
		{ 0,  0,  0,  0,  0}, /* CPU is active */
		{-1, -1, -1, -1, -1}, /* P-States */
		{-2, -2, -2, -2, -2}, /* C-States */
	}, /* ClusterB, 3x A7 */
};

#define cpu_set_valid(CLUSTER, CPU) \
	(cluster_status[CLUSTER].cpu_valid[CPU] = 1)
#define cpu_valid(CPU) \
	(cluster_status[cluster(CPU)].cpu_valid[CPU] == 1)
#define cpu_initialized(CPU) \
	(cluster_status[cluster(CPU)].cpu_idle[CPU] != -2)
#define same_cluster(CPU1, CPU2) \
	(cluster(CPU1) == cluster(CPU2))

#define cpu_set_running(CPU) \
	(cluster_status[cluster(CPU)].cpu_active[CPU] = 1)
#define cpu_set_sleeping(CPU) \
	(cluster_status[cluster(CPU)].cpu_active[CPU] = 0)

#define cpu_running(CPU) \
	(cluster_status[cluster(CPU)].cpu_active[CPU] == 1)
#define cpu_sleeping(CPU) \
	(cluster_status[cluster(CPU)].cpu_active[CPU] == 0)

#define cpu_freq(CPU) \
	cluster_status[cluster(CPU)].cpu_freq[CPU]
#define cpu_idle(CPU) \
	cluster_status[cluster(CPU)].cpu_idle[CPU]

#define cluster_pstate(CPU) \
	cluster_status[cluster(CPU)].cluster_pstate
#define cluster_cstate(CPU) \
	cluster_status[cluster(CPU)].cluster_cstate

#define cluster_cpus_freq(CPU) \
	cluster_status[cluster(CPU)].cpu_freq
#define cluster_cpus_idle(CPU) \
	cluster_status[cluster(CPU)].cpu_idle

#define cluster_gplot_freq(CPU) \
	cluster_status[cluster(CPU)].gnuplot_freq_fd
#define cluster_gplot_idle(CPU) \
	cluster_status[cluster(CPU)].gnuplot_idle_fd

#define EVENT_IDLE_FORMAT "     idlestat/vex-tc2  [%03d] .... %12.6f: cpu_idle: state=%u cpu_id=%u\n"
#define EVENT_FREQ_FORMAT "     idlestat/vex-tc2  [%03d] .... %12.6f: cpu_frequency: state=%u cpu_id=%u\n"
#define EVENT_MARK_FORMAT "     idlestat/vex-tc2  [%03d] .... %12.6f: idlestat_%s\n"

#define EVENT_FREQ_FDEBUG "     idlestat/vex-tc2  [%03d] .... %12.6f: %s frequency: state=%u cpu_id=%u\n"
#define EVENT_IDLE_FDEBUG "     idlestat/vex-tc2  [%03d] .... %12.6f: %s idle: state=%u cpu_id=%u\n"
#define EVENT_MARK_FDEBUG "     idlestat/vex-tc2  [%03d] .... %12.6f: %s idlestat_%s\n"

int get_min_cstate(int cpu)
{
	int i;
	int min_cstate = INT_MAX;

	for (i = 0; i < 5; ++i) {
		/* Disregard CPUs not in that cluster or not initialized */
		if (!same_cluster(cpu, i) || !cpu_initialized(i))
			continue;
		/* Running CPUs always enforeces WFI status */
		if (cpu_running(i))
			return 0;
		if (min_cstate > cpu_idle(i))
			min_cstate = cpu_idle(i);
	}

	return min_cstate;
}

int get_max_pstate(int cpu)
{
	int i;
	int max_pstate = 0;

	for (i = 0; i < 5; ++i) {
		/* Disregard CPUs idle or not in that cluster */
		if (!same_cluster(cpu, i) || cpu_sleeping(i))
			continue;
		/* Disregard CPUs not initialized */
		if (!cpu_initialized(i))
			continue;
		if (max_pstate < cpu_freq(i))
			max_pstate = cpu_freq(i);
	}

	return max_pstate;
}

void dump_psci_proxy_status()
{
	int i;

	for (i = 0; i < 2; ++i) {
		/* CPUs Pstates */
		print_vrb("Cluster%c P-States, CPUs: {%8d %8d %8d %8d %8d}, Cluster: %8d\n",
				i ? 'B' : 'A',
				cluster_status[i].cpu_freq[0],
				cluster_status[i].cpu_freq[1],
				cluster_status[i].cpu_freq[2],
				cluster_status[i].cpu_freq[3],
				cluster_status[i].cpu_freq[4],
				cluster_status[i].cluster_pstate
			 );
		/* CPUs Cstates */
		print_vrb("Cluster%c C-States, CPUs: {%8d %8d %8d %8d %8d}, Cluster: %8d\n",
				i ? 'B' : 'A',
				cluster_status[i].cpu_idle[0],
				cluster_status[i].cpu_idle[1],
				cluster_status[i].cpu_idle[2],
				cluster_status[i].cpu_idle[3],
				cluster_status[i].cpu_idle[4],
				cluster_status[i].cluster_cstate
			 );
	}
}

bool init_completed = false;

void dump_gnuplot_idle(double time, unsigned int cpu)
{
	char gpfilename[] = "./ClusterA_CStates.dat";
	int cluster_id = cluster(cpu);

	if (!init_completed)
		return;

	if (cluster_gplot_idle(cpu) == NULL) {
		gpfilename[9] = 'A' + cluster_id;
		print_vrb("Opening GPlot data [%s]...\n", gpfilename);
		cluster_gplot_idle(cpu) = fopen(gpfilename, "w");
	}

	fprintf(cluster_gplot_idle(cpu),
		"%.6f %8d %8d %8d %8d %8d %8d\n",
		time,
		cluster_status[cluster_id].cpu_idle[0],
		cluster_status[cluster_id].cpu_idle[1],
		cluster_status[cluster_id].cpu_idle[2],
		cluster_status[cluster_id].cpu_idle[3],
		cluster_status[cluster_id].cpu_idle[4],
		cluster_status[cluster_id].cluster_cstate
		);
}

void dump_gnuplot_freq(double time, unsigned int cpu)
{
	char gpfilename[] = "./ClusterA_PStates.dat";
	int cluster_id = cluster(cpu);

	if (!init_completed)
		return;

	if (cluster_gplot_freq(cpu) == NULL) {
		gpfilename[9] = 'A' + cluster_id;
		print_vrb("Opening GPlot data [%s]...\n", gpfilename);
		cluster_gplot_freq(cpu) = fopen(gpfilename, "w");
	}

	fprintf(cluster_gplot_freq(cpu),
		"%.6f %8d %8d %8d %8d %8d %8d\n",
		time,
		cluster_status[cluster_id].cpu_freq[0],
		cluster_status[cluster_id].cpu_freq[1],
		cluster_status[cluster_id].cpu_freq[2],
		cluster_status[cluster_id].cpu_freq[3],
		cluster_status[cluster_id].cpu_freq[4],
		cluster_status[cluster_id].cluster_pstate
		);
}

void close_gnuplot_data()
{
	if (cluster_status[0].gnuplot_freq_fd != NULL)
		fclose(cluster_status[0].gnuplot_freq_fd);
	if (cluster_status[0].gnuplot_idle_fd != NULL)
		fclose(cluster_status[0].gnuplot_idle_fd);
	if (cluster_status[1].gnuplot_freq_fd != NULL)
		fclose(cluster_status[1].gnuplot_freq_fd);
	if (cluster_status[1].gnuplot_idle_fd != NULL)
		fclose(cluster_status[1].gnuplot_idle_fd);
}

void switch_cluster_cstate(FILE *f, double time, unsigned int state, unsigned int cpu)
{
	int i;

	/* Keep track of new cluster C-State */
	cluster_cstate(cpu) = state;

	print_vrb("Switch Cluster%c idle state to %s (state %d)\n",
			'A'+cluster(cpu),
			state ? "C1" : "WFI",
			state);

	for (i = 0; i < 5; ++i) {
		/* Disregard CPUs not in that cluster or not idle */
		if (!same_cluster(cpu, i)
			|| !cpu_sleeping(i)
			|| !cpu_initialized(i))
			continue;

		/* Generate a fake C-State exit event which is*/
		/* required to close the previous period */
		if (i != cpu) {
			/* This is required just for other CPUs of this cluster */
			/* since the current CPU has been already traced in the */
			/* calling method */
			fprintf(f, EVENT_IDLE_FORMAT, i, time, -1, i);
			print_vrb(EVENT_IDLE_FDEBUG, i, time, ">>>", -1, i);
		}

		/* This is for sure:
		 * - a CPU of this cluster
		 * - which is idle
		 * => switch event to new cluster idle state */
		fprintf(f, EVENT_IDLE_FORMAT, i, time, state, i);
		print_vrb(EVENT_IDLE_FDEBUG, i, time, ">>>", state, i);
	}

	dump_psci_proxy_status();

}

/* This is called on C-State enter. Such events are alwasy repored in the output */
/* trace after having properly tuned the C-State the CPU is entering, which could */
/* be never higher than the Cluster C-State. */
void update_cstate(FILE *f, double time, unsigned int state, unsigned int cpu)
{

	/* Sanity check we update only CPUs of that cluster */
	assert(cpu_valid(cpu));

	/* Update PSCI-Proxy C-State for that CPU */
	cpu_idle(cpu) = state;

	/* C-State EXIT */
	if (state == -1) {

		/* Trace the CPU active event */
		fprintf(f, EVENT_IDLE_FORMAT, cpu, time, state, cpu);
		print_vrb(EVENT_IDLE_FDEBUG, cpu, time, ">>>", state, cpu);

		/* Notify all other idle CPUs that now on the cluster
		 * has been switched to C0 */
		if (cluster_cstate(cpu) > 0)
			switch_cluster_cstate(f, time, 0, cpu);

		/* A running CPU keeps the cluster not lower than C0 */
		cpu_idle(cpu) = 0;

		return;
	}

	/* C-State ENTER */

	/* Mark the CPU as running */
	cpu_set_sleeping(cpu);

	/* If the CPU is entering a deeper C-State than the cluster one:
	   => notify just the CPU entering the Cluster idle state */
	if (cluster_cstate(cpu) == get_min_cstate(cpu)) {
		fprintf(f, EVENT_IDLE_FORMAT, cpu, time, cluster_cstate(cpu), cpu);
		print_vrb(EVENT_IDLE_FDEBUG, cpu, time, ">>>", cluster_cstate(cpu), cpu);
		goto exit_plot;
	}

	/* The CPU is entering a C-State which is lower than the Cluster one:
	   => all idle CPUs enters this new higer C-State */
	switch_cluster_cstate(f, time, state, cpu);

exit_plot:
	/* Just for debuggin */
	dump_gnuplot_idle(time, cpu);

	return;
}

void switch_cluster_pstate(FILE *f, double time, unsigned int freq, unsigned int cpu)
{
	int i;

	/* Keep track of new cluster p_State */
	cluster_pstate(cpu) = freq;

	print_vrb("Switch Cluster%c frequency to %u Hz\n",
			'A'+cluster(cpu), freq);

	/* All active CPUs switching to the new Cluster frequency */
	for (i = 0; i < 5; ++i) {
		/* Disregard CPUs not in that cluster or not running */
		if (!same_cluster(cpu, i)
			|| cpu_sleeping(i)
			|| !cpu_initialized(i))
			continue;
		/* This is for sure:
		 * - a CPU of this cluster
		 * - which is not idle
		 * => switch event to new cluster frequency */
		fprintf(f, EVENT_FREQ_FORMAT, i, time, freq, i);
		print_vrb(EVENT_FREQ_FDEBUG, i, time, ">>>", freq, i);
	}

	dump_psci_proxy_status();

}

void update_pstate(FILE *f, double time, unsigned int freq, unsigned int cpu)
{
	int max_pstate;

	/* Sanity check we update only CPUs of that cluster */
	assert(cpu_valid(cpu));

	/* Update PSCI-Proxy PState for that CPU */

	/* This method is called also whan a CPU is entering an idle state
	 * in that cases the current CPU frequency is not going to be updated
	 * since at wake-up it will re-enter that frequency */
	if (cpu_running(cpu) && freq != 0)
		cpu_freq(cpu) = freq;

	/* If this is the last CPU of a cluster to enter idle, we do not need
	 * to generate any additional event, just report event for plotting */
	max_pstate = get_max_pstate(cpu);
	if (max_pstate == 0)
		goto exit_plot;

	/* If this CPU is going to sleep:
	 * update frequency of other active CPUs */
	if (cpu_sleeping(cpu)) {
		if (cluster_pstate(cpu) == max_pstate)
			return;
		switch_cluster_pstate(f, time, max_pstate, cpu);
		goto exit_plot;
	}

	/* If the CPU is entering a lower P-State than the cluster one:
	   => notify just the CPU entering the higer Cluster frequency */
	if (cluster_pstate(cpu) == max_pstate) {
		fprintf(f, EVENT_FREQ_FORMAT, cpu, time, cluster_pstate(cpu), cpu);
		print_vrb(EVENT_FREQ_FDEBUG, cpu, time, ">>>", cluster_pstate(cpu), cpu);
		goto exit_plot;
	}

	/* The CPU is entering a P-State which is higher than the Cluster one:
	   => all active CPUs enters this new higer P-State */
	switch_cluster_pstate(f, time, max_pstate, cpu);

exit_plot:
	/* Just for debugging */
	dump_gnuplot_freq(time, cpu);

	return;
}

extern struct cpu_topology g_cpu_topo_list;

void setup_mapping()
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;
	struct cpu_cpu      *s_cpu;
	int i = 0;

	list_for_each_entry(s_phy, &g_cpu_topo_list.physical_head, list_physical) {
		list_for_each_entry(s_core, &s_phy->core_head, list_core) {
			list_for_each_entry(s_cpu, &s_core->cpu_head, list_cpu) {

				/* TC2 specific boundaries */
				assert(s_phy->physical_id < 2);
				assert(s_cpu->cpu_id < 5);

				cpu_to_cluster[s_cpu->cpu_id] = s_phy->physical_id;
				cluster_status[s_phy->physical_id].cpu_idle[i] = -1;
				cpu_set_valid(s_phy->physical_id, s_cpu->cpu_id);

				print_vrb("Mapping CPU%d on Cluster%c\n",
						s_cpu->cpu_id,
						'A' + s_phy->physical_id);

				++i;
			}
		}
	}

	/* TC2 specific check for expected number of mappings */
	assert(i == 5);

  }

int store_line(const char *line, void *data)
{
	unsigned int state = 0, freq = 0, cpu = 0;
	FILE *f = data;
	double time;

	/* ignore comment line */
	if (line[0] == '#')
		return 0;

	/* Filter CPUIdle events */
	if (strstr(line, "cpu_idle")) {
		assert(sscanf(line, TRACE_FORMAT, &time, &state, &cpu) == 3);

		/* Just for debug: report event on the output trace */
		print_vrb(EVENT_IDLE_FDEBUG, cpu, time, "<<<", state, cpu);

		/* C-State enter are alwasy filtered and repored in output */
		update_cstate(f, time, state, cpu);
		return 0;
	}

	/* Filter CPUFreq events */
	if (strstr(line, "cpu_frequency") ||
			strstr(line, "idlestat_frequency")) {
		assert(sscanf(line, TRACE_FORMAT, &time, &freq, &cpu) == 3);

		/* Just for debug: report event on the output trace */
		print_vrb(EVENT_FREQ_FDEBUG, cpu, time, "<<<", freq, cpu);

		/* P-State variations alwasy filtered and (eventually) repored in output */
		update_pstate(f, time, freq, cpu);
		return 0;

	}

	/* Reformat idlestat START marker */
	if (strstr(line, "idlestat_start")) {
		assert(sscanf(line, TRACE_TIME_FORMAT, &cpu, &time) == 2);

		/* Just for debug: report event on the output trace */
		print_vrb(EVENT_MARK_FDEBUG, cpu, time, "<<<", "start");

		fprintf(f, EVENT_MARK_FORMAT, cpu, time, "start");
		print_vrb(EVENT_MARK_FDEBUG, cpu, time, ">>>", "start");

		init_completed = true;

		/* Dump initial configuration */
		for (cpu = 0; cpu < 5 && cluster_status[0].cpu_valid[cpu]; ++cpu)
			; /* Find first CPU con ClusterA */
		dump_gnuplot_idle(time, cpu);
		dump_gnuplot_freq(time, cpu);
		for (cpu = 0; cpu < 5 && cluster_status[1].cpu_valid[cpu]; ++cpu)
			; /* Find first CPU con ClusterB */
		dump_gnuplot_idle(time, cpu);
		dump_gnuplot_freq(time, cpu);

		/* dump_psci_proxy_status(); */

		return 0;
	}

	/* Reformat idlestat END marker */
	if (strstr(line, "idlestat_end")) {
		assert(sscanf(line, TRACE_TIME_FORMAT, &cpu, &time) == 2);

		/* Just for debug: report event on the output trace */
		print_vrb(EVENT_MARK_FDEBUG, cpu, time, "<<<", "end");

		fprintf(f, EVENT_MARK_FORMAT, cpu, time, "end");
		print_vrb(EVENT_MARK_FDEBUG, cpu, time, ">>>", "end");
		return 0;
	}

	/* All other events are reporetd in output */
	fprintf(f, "%s", line);
	return 0;
}
#else
void setup_mapping()
{
}

void close_gnuplot_data()
{
}

int store_line(const char *line, void *data)
{
	FILE *f = data;

	/* ignore comment line */
	if (line[0] == '#')
		return 0;

	fprintf(f, "%s", line);

	return 0;
}
#endif

/*
 * This functions is a helper to read a specific file content and store
 * the content inside a variable pointer passed as parameter, the format
 * parameter gives the variable type to be read from the file.
 *
 * @path : directory path containing the file
 * @name : name of the file to be read
 * @format : the format of the format
 * @value : a pointer to a variable to store the content of the file
 * Returns 0 on success, -1 otherwise
 */
int file_read_value(const char *path, const char *name,
			const char *format, void *value)
{
	FILE *file;
	char *rpath;
	int ret;

	ret = asprintf(&rpath, "%s/%s", path, name);
	if (ret < 0)
		return ret;

	file = fopen(rpath, "r");
	if (!file) {
		ret = -1;
		goto out_free;
	}

	ret = fscanf(file, format, value) == EOF ? -1 : 0;

	fclose(file);
out_free:
	free(rpath);
	return ret;
}
