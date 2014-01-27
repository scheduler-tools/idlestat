/*
 *  idlestat.c
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
#define _GNU_SOURCE
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sched.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>

#include "idlestat.h"
#include "utils.h"
#include "trace.h"
#include "list.h"
#include "topology.h"

#define IDLESTAT_VERSION "0.3-rc1"

static char irq_type_name[][8] = {
			"irq",
			"ipi",
		};

static char buffer[BUFSIZE];

static inline int error(const char *str)
{
	perror(str);
	return -1;
}

static inline void *ptrerror(const char *str)
{
	perror(str);
	return NULL;
}

static int dump_cstates(struct cpuidle_cstates *cstates, int state,
			int count, char *str)
{
	int j, k;
	struct cpuidle_cstate *cstate;

	for (j = 0; j < cstates->cstate_max + 1; j++) {

		if (state != -1 && state != j)
			continue;

		cstate = &cstates->cstate[j];

		for (k = 0; k < MIN(count, cstate->nrdata); k++) {
			printf("%lf %d\n", cstate->data[k].begin, j);
			printf("%lf 0\n", cstate->data[k].end);
		}

		/* add a break */
		printf("\n");
	}

	return 0;
}

static int display_cstates(struct cpuidle_cstates *cstates, int state,
			int count, char *str)
{
	int j;
	struct cpuidle_cstate *cstate;
	struct wakeup_info *wakeinfo;
	struct wakeup_irq *irqinfo;

	for (j = 0; j < cstates->cstate_max + 1; j++) {

		if (state != -1 && state != j)
			continue;

		cstate = &cstates->cstate[j];

		printf("%s", str);
		printf("/state%d, %d hits, total %.2lfus," \
		       "avg %.2lfus, min %.2lfus, max %.2lfus\n",
		       j, cstate->nrdata, cstate->duration,
		       cstate->avg_time, cstate->min_time,
		       cstate->max_time);
	}

	if (strstr(str, IRQ_WAKEUP_UNIT_NAME)) {
		wakeinfo = &cstates->wakeinfo;
		irqinfo = wakeinfo->irqinfo;
		for (j = 0; j < wakeinfo->nrdata; j++, irqinfo++) {
			printf("\t%s", str);
			printf("/%s id %d, name %s, wakeup count %d\n",
				(irqinfo->irq_type < IRQ_TYPE_MAX) ? irq_type_name[irqinfo->irq_type] : "NULL",
				irqinfo->id, irqinfo->name, irqinfo->count);
		}
	}

	return 0;
}

int dump_all_data(struct cpuidle_datas *datas, int state, int count,
		int (*dump)(struct cpuidle_cstates *, int,  int, char *))
{
	int i = 0, nrcpus = datas->nrcpus;
	struct cpuidle_cstates *cstates;

	do {
		cstates = &datas->cstates[i];

		if (nrcpus == -1)
			sprintf(buffer, "cluster");
		else
			sprintf(buffer, "cpu%d", i);

		dump(cstates, state, count, buffer);

		i++;

	} while (i < nrcpus && nrcpus != -1);

	return 0;
}

static struct cpuidle_data *intersection(struct cpuidle_data *data1,
					 struct cpuidle_data *data2)
{
	double begin, end;
	struct cpuidle_data *data;

	begin = MAX(data1->begin, data2->begin);
	end = MIN(data1->end, data2->end);

	if (begin >= end)
		return NULL;

	data = malloc(sizeof(*data));
	if (!data)
		return NULL;

	data->begin = begin;
	data->end = end;
	data->duration = end - begin;
	data->duration *= 1000000;

	return data;
}

static struct cpuidle_cstate *inter(struct cpuidle_cstate *c1,
				    struct cpuidle_cstate *c2)
{
	int i, j;
	struct cpuidle_data *interval;
	struct cpuidle_cstate *result;
	struct cpuidle_data *data = NULL;
	size_t index;

	if (!c1)
		return c2;
	if (!c2)
		return c1;

	result = calloc(sizeof(*result), 1);
	if (!result)
		return NULL;

	for (i = 0, index = 0; i < c1->nrdata; i++) {

		for (j = index; j < c2->nrdata; j++) {

			/* intervals are ordered, no need to go further */
			if (c1->data[i].end < c2->data[j].begin)
				break;

			/* primary loop begins where we ended */
			if (c1->data[i].begin > c2->data[j].end)
				index = j;

			interval = intersection(&c1->data[i], &c2->data[j]);
			if (!interval)
				continue;

			result->min_time = MIN(!result->nrdata ? 999999.0 :
					       result->min_time,
					       interval->duration);

			result->max_time = MAX(result->max_time,
					       interval->duration);

			result->avg_time = AVG(result->avg_time,
					       interval->duration,
					       result->nrdata + 1);

			result->duration += interval->duration;

			result->nrdata++;

			data = realloc(data, sizeof(*data) *
				       (result->nrdata + 1));
			if (!data)
				return NULL;

			result->data = data;
			result->data[result->nrdata - 1] = *interval;

			free(interval);
		}
	}

	return result;
}

static int store_data(double time, int state, int cpu,
		      struct cpuidle_datas *datas, int count)
{
	struct cpuidle_cstates *cstates = &datas->cstates[cpu];
	struct cpuidle_cstate *cstate;
	struct cpuidle_data *data;
	int nrdata, last_cstate = cstates->last_cstate;

	/* ignore when we got a "closing" state first */
	if (state == -1 && !cstates->cstate_max)
		return 0;

	cstate = &cstates->cstate[state == -1 ? last_cstate : state ];
	data = cstate->data;
	nrdata = cstate->nrdata;

	if (state == -1) {

		data = &data[nrdata];

		data->end = time;
		data->duration = data->end - data->begin;

		/* That happens when precision digit in the file exceed
		 * 7 (eg. xxx.1000000). Ignoring the result because I don't
		 * find a way to fix with the sscanf used in the caller
		 */
		if (data->duration < 0)
			return 0;

		/* convert to us */
		data->duration *= 1000000;

		cstate->min_time = MIN(!nrdata ? 999999.0 : cstate->min_time,
				       data->duration);

		cstate->max_time = MAX(cstate->max_time, data->duration);


		cstate->avg_time = AVG(cstate->avg_time, data->duration,
				       cstate->nrdata + 1);

		cstate->duration += data->duration;

		cstate->nrdata++;

		return 0;
	}

	data = realloc(data, sizeof(*data) * (nrdata + 1));
	if (!data)
		return error("realloc data");;

	data[nrdata].begin = time;

	cstates->cstate[state].data = data;
	cstates->cstate_max = MAX(cstates->cstate_max, state);
	cstates->last_cstate = state;
	cstates->wakeirq = NULL;

	return 0;
}

static struct wakeup_irq *find_irqinfo(struct wakeup_info *wakeinfo, int irqid)
{
	struct wakeup_irq *irqinfo;
	int i;

	for (i = 0; i < wakeinfo->nrdata; i++) {
		irqinfo = &wakeinfo->irqinfo[i];
		if (irqinfo->id == irqid)
			return irqinfo;
	}

	return NULL;
}

static int store_irq(int cpu, int irqid, char *irqname,
		      struct cpuidle_datas *datas, int count, int irq_type)
{
	struct cpuidle_cstates *cstates = &datas->cstates[cpu];
	struct wakeup_irq *irqinfo;
	struct wakeup_info *wakeinfo = &cstates->wakeinfo;

	if (cstates->wakeirq != NULL)
		return 0;

	irqinfo = find_irqinfo(wakeinfo, irqid);
	if (NULL == irqinfo) {
		irqinfo = realloc(wakeinfo->irqinfo,
				sizeof(*irqinfo) * (wakeinfo->nrdata + 1));
		if (!irqinfo)
			return error("realloc irqinfo");

		wakeinfo->irqinfo = irqinfo;

		irqinfo = &wakeinfo->irqinfo[wakeinfo->nrdata++];
		irqinfo->id = irqid;
		strcpy(irqinfo->name, irqname);
		irqinfo->irq_type = irq_type;
		irqinfo->count = 0;
	}

	irqinfo->count++;

	cstates->wakeirq = irqinfo;

	return 0;
}

#define TRACE_IRQ_FORMAT "%*[^[][%d] %*[^=]=%d%*[^=]=%16s"
#define TRACE_IPIIRQ_FORMAT "%*[^[][%d] %*[^=]=%d%*[^=]=%16s"

#define TRACE_CMD_FORMAT "%*[^]]] %lf:%*[^=]=%u%*[^=]=%d"
#define TRACE_FORMAT "%*[^]]] %*s %lf:%*[^=]=%u%*[^=]=%d"

static int get_wakeup_irq(struct cpuidle_datas *datas, char *buffer, int count)
{
	int cpu, irqid;
	char irqname[NAMELEN+1];

	if (strstr(buffer, "irq_handler_entry")) {
		sscanf(buffer, TRACE_IRQ_FORMAT, &cpu, &irqid, irqname);

		store_irq(cpu, irqid, irqname, datas, count, HARD_IRQ);
		return 0;
	}

	if (strstr(buffer, "ipi_handler_entry")) {
		sscanf(buffer, TRACE_IPIIRQ_FORMAT, &cpu, &irqid, irqname);

		store_irq(cpu, irqid, irqname, datas, count, IPI_IRQ);
		return 0;
	}

	return -1;
}


static struct cpuidle_datas *idlestat_load(const char *path)
{
	FILE *f;
	unsigned int state = 0, cpu = 0, nrcpus= 0;
	double time, begin = 0, end = 0;
	size_t count, start = 1;
	struct cpuidle_datas *datas;
	int ret;

	f = fopen(path, "r");
	if (!f)
		return ptrerror("fopen");

	for (count = 0; count < 2; count++) {
		fgets(buffer, BUFSIZE, f);
		sscanf(buffer, "cpus=%u", &nrcpus);
	}

	if (!nrcpus)
		return ptrerror("read error for 'cpus=' in trace file");

	datas = malloc(sizeof(*datas));
	if (!datas)
		return ptrerror("malloc datas");

	datas->cstates = calloc(sizeof(*datas->cstates), nrcpus);
	if (!datas->cstates)
		return ptrerror("calloc cstate");

	datas->nrcpus = nrcpus;

	fgets(buffer, BUFSIZE, f);

	/* read topology information */
	read_cpu_topo_info(f, buffer);

	do {
		if (strstr(buffer, "cpu_idle")) {
			sscanf(buffer, TRACE_FORMAT, &time, &state, &cpu);

			if (start) {
				begin = time;
				start = 0;
			}
			end = time;

			store_data(time, state, cpu, datas, count);
			count++;
			continue;
		}

		ret = get_wakeup_irq(datas, buffer, count);
		count += (0 == ret) ? 1 : 0;

	} while (fgets(buffer, BUFSIZE, f));

	fclose(f);

	fprintf(stderr, "Log is %lf secs long with %d events\n",
		end - begin, (int)count);

	return datas;
}

struct cpuidle_datas *cluster_data(struct cpuidle_datas *datas)
{
	struct cpuidle_cstate *c1, *cstates;
	struct cpuidle_datas *result;
	int i, j;
	int cstate_max = -1;

	result = malloc(sizeof(*result));
	if (!result)
		return NULL;

	result->nrcpus = -1; /* the cluster */

	result->cstates = calloc(sizeof(*result->cstates), 1);
	if (!result->cstates)
		return NULL;

	/* hack but negligeable overhead */
	for (i = 0; i < datas->nrcpus; i++)
		cstate_max = MAX(cstate_max, datas->cstates[i].cstate_max);
	result->cstates[0].cstate_max = cstate_max;

	for (i = 0; i < cstate_max + 1; i++) {

		for (j = 0, cstates = NULL; j < datas->nrcpus; j++) {

			c1 = &datas->cstates[j].cstate[i];

			cstates = inter(cstates, c1);
			if (!cstates)
				continue;
		}

		result->cstates[0].cstate[i] = *cstates;
	}

	return result;
}

struct cpuidle_cstates *core_cluster_data(struct cpu_core *s_core)
{
	struct cpuidle_cstate *c1, *cstates;
	struct cpuidle_cstates *result;
	struct cpu_cpu      *s_cpu;
	int i;
	int cstate_max = -1;

	if (!s_core->is_ht)
		list_for_each_entry(s_cpu, &s_core->cpu_head, list_cpu)
			return s_cpu->cstates;

	result = calloc(sizeof(*result), 1);
	if (!result)
		return NULL;

	/* hack but negligeable overhead */
	list_for_each_entry(s_cpu, &s_core->cpu_head, list_cpu)
		cstate_max = MAX(cstate_max, s_cpu->cstates->cstate_max);
	result->cstate_max = cstate_max;

	for (i = 0; i < cstate_max + 1; i++) {
		cstates = NULL;
		list_for_each_entry(s_cpu, &s_core->cpu_head, list_cpu) {
			c1 = &s_cpu->cstates->cstate[i];

			cstates = inter(cstates, c1);
			if (!cstates)
				continue;
		}

		result->cstate[i] = *cstates;
	}

	return result;
}

struct cpuidle_cstates *physical_cluster_data(struct cpu_physical *s_phy)
{
	struct cpuidle_cstate *c1, *cstates;
	struct cpuidle_cstates *result;
	struct cpu_core      *s_core;
	int i;
	int cstate_max = -1;

	result = calloc(sizeof(*result), 1);
	if (!result)
		return NULL;

	/* hack but negligeable overhead */
	list_for_each_entry(s_core, &s_phy->core_head, list_core)
		cstate_max = MAX(cstate_max, s_core->cstates->cstate_max);
	result->cstate_max = cstate_max;

	for (i = 0; i < cstate_max + 1; i++) {
		cstates = NULL;
		list_for_each_entry(s_core, &s_phy->core_head, list_core) {
			c1 = &s_core->cstates->cstate[i];

			cstates = inter(cstates, c1);
			if (!cstates)
				continue;
		}

		result->cstate[i] = *cstates;
	}

	return result;
}

static void help(const char *cmd)
{
	fprintf(stderr, "%s [-d|--dump] [-c|--cstate=x] [-o|--output-file] <file>\n", basename(cmd));
}

static void version(const char *cmd)
{
	printf("%s version %s\n", basename(cmd), IDLESTAT_VERSION);
}

static struct option long_options[] = {
	{ "dump",        0, 0, 'd' },
	{ "iterations",  0, 0, 'i' },
	{ "cstate",      0, 0, 'c' },
	{ "debug",       0, 0, 'g' },
	{ "output-file", 0, 0, 'o' },
	{ "verbose",     0, 0, 'v' },
	{ "version",     0, 0, 'V' },
	{ "help",        0, 0, 'h' },
	{ 0,             0, 0, 0   }
};

struct idledebug_options {
	bool debug;
	bool dump;
	int cstate;
	int iterations;
	char *filename;
	unsigned int duration;
};

int getoptions(int argc, char *argv[], struct idledebug_options *options)
{
	int c;

	memset(options, 0, sizeof(*options));
	options->cstate = -1;
	options->filename = NULL;

	while (1) {

		int optindex = 0;

		c = getopt_long(argc, argv, "gdvVho:i:c:t:",
				long_options, &optindex);
		if (c == -1)
			break;

		switch (c) {
		case 'g':
			options->debug = true;
			break;
		case 'd':
			options->dump = true;
			break;
		case 'i':
			options->iterations = atoi(optarg);
			break;
		case 'c':
			options->cstate = atoi(optarg);
			break;
		case 't':
			options->duration = atoi(optarg);
			break;
		case 'o':
			options->filename = optarg;
			break;
		case 'h':
			help(argv[0]);
			exit(0);
			break;
		case 'V':
			version(argv[0]);
			exit(0);
			break;
		case '?':
			fprintf(stderr, "%s: Unknown option %c'.\n",
				argv[0], optopt);
		default:
			return -1;
		}
	}

	if (options->cstate >= MAXCSTATE) {
		fprintf(stderr, "C-state must be less than %d\n",
			MAXCSTATE);
		return -1;
	}

	if (options->iterations < 0) {
		fprintf(stderr, "dump values must be a positive value\n");
	}

	if (NULL == options->filename) {
		fprintf(stderr, "expected filename\n");
		return -1;
	}

	return 0;
}

static int idlestat_file_for_each_line(const char *path, void *data,
					int (*handler)(const char *, void *))
{
	FILE *f;
	int ret;

	if (!handler)
		return -1;

	f = fopen(path, "r");

	if (!f) {
		fprintf(f, "failed to open '%s': %m\n", path);
		return -1;
	}

	while (fgets(buffer, BUFSIZE, f)) {
		ret = handler(buffer, data);
		if (ret)
			break;
	}

	fclose(f);

	return ret;
}

static int idlestat_store(const char *path)
{
	FILE *f;
	int ret;

	ret = sysconf(_SC_NPROCESSORS_CONF);
	if (ret < 0)
		return -1;

	f = fopen(path, "w+");
	if (!f) {
		fprintf(f, "failed to open '%s': %m\n", path);
		return -1;
	}

	fprintf(f, "version = 1\n");
	fprintf(f, "cpus=%d\n", ret);

	/* output topology information */
	output_cpu_topo_info(f);

	ret = idlestat_file_for_each_line(TRACE_FILE, f, store_line);

	fclose(f);

	return ret;
}

static int idlestat_wake_all(void)
{
	int rcpu, i, ret;
	cpu_set_t cpumask;

	ret = sysconf(_SC_NPROCESSORS_CONF);
	if (ret < 0)
		return -1;

	rcpu = sched_getcpu();
	if (rcpu < 0)
		return -1;

	for (i = 0; i < ret; i++) {

		/* Pointless to wake up ourself */
		if (i == rcpu)
			continue;

		CPU_ZERO(&cpumask);
		CPU_SET(i, &cpumask);

		sched_setaffinity(0, sizeof(cpumask), &cpumask);
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct cpuidle_datas *datas;
	struct cpuidle_datas *cluster;
	struct idledebug_options options;
	struct rusage rusage;

	if (getoptions(argc, argv, &options))
		return 1;

	/* We have to manipulate some files only accessible to root */
	if (getuid()) {
		fprintf(stderr, "must be root to run the tool\n");
		return -1;
	}

	/* init cpu topoinfo */
	init_cpu_topo_info();

	/* Acquisition time specified means we will get the traces */
	if (options.duration) {

		/* Read cpu topology info from sysfs */
		read_sysfs_cpu_topo();

		/* Stop tracing (just in case) */
		if (idlestat_trace_enable(false))
			return -1;

		/* Initialize the traces for cpu_idle and increase the
		 * buffer size to let 'idlestat' to sleep instead of
		 * acquiring data, hence preventing it to pertubate the
		 * measurements. */
		if (idlestat_init_trace(options.duration))
			return 1;

		/* Remove all the previous traces */
		if (idlestat_flush_trace())
			return -1;

		/* Start the recording */
		if (idlestat_trace_enable(true))
			return -1;

		/* We want to prevent to begin the acquisition with a cpu in
		 * idle state because we won't be able later to close the
		 * state and to determine which state it was. */
		if (idlestat_wake_all())
			return -1;

		/* Do nothing */
		sleep(options.duration);

		/* Stop tracing */
		if (idlestat_trace_enable(false))
			return -1;

		/* At this point we should have some spurious wake up
		 * at the beginning of the traces and at the end (wake
		 * up all cpus and timer expiration for the timer
		 * acquisition). We assume these will be lost in the number
		 * of other traces and could be negligible. */
		if (idlestat_store(options.filename))
			return -1;
	}

	/* Load the idle states information */
	datas = idlestat_load(options.filename);
	if (!datas)
		return 1;

	/* Compute cluster idle intersection between cpus belonging to
	 * the same cluster
	 */
	if (0 == establish_idledata_to_topo(datas)) {
		if (options.dump > 0)
			dump_cpu_topo_info(options.cstate, options.iterations,
						dump_cstates);
		else
			dump_cpu_topo_info(options.cstate, options.iterations,
						display_cstates);
	} else {
		cluster = cluster_data(datas);
		if (!cluster)
			return 1;

		if (options.dump > 0) {
			dump_all_data(datas, options.cstate,
					options.iterations, dump_cstates);
			dump_all_data(cluster, options.cstate,
					options.iterations, dump_cstates);
		} else {
			dump_all_data(datas, options.cstate,
					options.iterations, display_cstates);
			dump_all_data(cluster, options.cstate,
					options.iterations, display_cstates);
		}

		free(cluster->cstates);
		free(cluster);
	}

	/* Computation could be heavy, let's give some information
	 * about the memory consumption */
	if (options.debug) {
		getrusage(RUSAGE_SELF, &rusage);
		printf("max rss : %ld kB\n", rusage.ru_maxrss);
	}

	release_cpu_topo_cstates();
	release_cpu_topo_info();

	return 0;
}
