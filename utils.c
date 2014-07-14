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

#include "utils.h"

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

/* Temporary globals, to be better organized */
int cpu_to_cluster[] = {1,0,0,1,1};
#define cluster(CPU) cpu_to_cluster[CPU]
#define cluster_label(CPU) 'A'+cpu_to_cluster[CPU]

/* Keep track of each cluster status  */
struct cluster {
	FILE *gnuplot_freq_fd;
	FILE *gnuplot_idle_fd;
	int cluster_pstate; /* Cluster current P state */
	int cluster_cstate; /* Cluster current C state */
	/* for simplicity, use absolute CPU_ID as index */
	int cpu_freq[5]; /* freq  0: CPU is idle */
	int cpu_idle[5]; /* idle 99: CPU is not in that cluster */
		         /* idle -1: CPU ative or in unknowen idle state */

} cluster_status[] = {
	{NULL, NULL, 0, 99,
		{ 0,  0,  0,  0,  0}, /* P-States */
		{99, -1, -1, 99, 99}, /* C-States */
	}, /* ClusterA, 2x A15 */
	{NULL, NULL, 0, 99,
		{ 0,  0,  0,  0,  0}, /* P-States */
		{-1, 99, 99, -1, -1}, /* C-States */
	}, /* ClusterB, 3x A7 */
};

#define cpu_freq(CPU) \
	cluster_status[cluster(CPU)].cpu_freq[CPU]
#define cpu_idle(CPU) \
	cluster_status[cluster(CPU)].cpu_idle[CPU]

#define cpu_freq_valid(CPU) \
	(cluster_status[cluster(CPU)].cpu_freq[CPU] !=  0)
#define cpu_idle_valid(CPU) \
	(cluster_status[cluster(CPU)].cpu_idle[CPU] != -1)

#define cluster_pstate(CPU) \
	cluster_status[cluster(CPU)].cluster_pstate
#define cluster_cstate(CPU) \
	cluster_status[cluster(CPU)].cluster_cstate

#define cluster_cpu_freq(CLUSTER, CPU) \
	cluster_status[CLUSTER].cpu_freq[CPU]
#define cluster_cpu_idle(CLUSTER, CPU) \
	cluster_status[CLUSTER].cpu_idle[CPU]

#define cluster_cpus_freq(CPU) \
	cluster_status[cluster(CPU)].cpu_freq
#define cluster_cpus_idle(CPU) \
	cluster_status[cluster(CPU)].cpu_idle

#define cluster_gplot_freq(CPU) \
	cluster_status[cluster(CPU)].gnuplot_freq_fd
#define cluster_gplot_idle(CPU) \
	cluster_status[cluster(CPU)].gnuplot_idle_fd


#define EVENT_FREQ_FDEBUG "     idlestat/vex-tc2  [%03d] .... %12.6f: %s frequency: state=%u cpu_id=%u\n"
#define EVENT_IDLE_FDEBUG "     idlestat/vex-tc2  [%03d] .... %12.6f: %s idle: state=%u cpu_id=%u\n"

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

	}

	/* Filter CPUFreq events */
	if (strstr(line, "cpu_frequency") ||
			strstr(line, "idlestat_frequency")) {
		assert(sscanf(line, TRACE_FORMAT, &time, &freq, &cpu) == 3);

		/* Just for debug: report event on the output trace */
		print_vrb(EVENT_FREQ_FDEBUG, cpu, time, "<<<", freq, cpu);


	}

	/* All other events are reporetd in output */
	fprintf(f, "%s", line);
	return 0;
}
#else
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
