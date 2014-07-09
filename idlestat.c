/*
 *  idlestat.c
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
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sched.h>
#include <string.h>
#include <float.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <assert.h>

#include "idlestat.h"
#include "utils.h"
#include "trace.h"
#include "list.h"
#include "topology.h"
#include "energy_model.h"

#define IDLESTAT_VERSION "0.4-rc1"
#define USEC_PER_SEC 1000000

/* Configuration Options */
struct program_options options;

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

static int dump_states(struct cpuidle_cstates *cstates,
		       struct cpufreq_pstates *pstates,
		       int count, char *str)
{
	int j, k, kmax;
	struct cpuidle_cstate *cstate;

	for (j = 0; j < cstates->cstate_max + 1; j++) {
		cstate = &cstates->cstate[j];

		kmax = count > 0 ? MIN(count, cstate->nrdata) : cstate->nrdata;
		for (k = 0; k < kmax; k++) {
			printf("%lf: enter %s\n", cstate->data[k].begin,
			       cstate->name);
			printf("%lf: exit %s\n", cstate->data[k].end,
			       cstate->name);
		}

		/* add a break */
		printf("\n");
	}

	return 0;
}

static int display_states(struct cpuidle_cstates *cstates,
			  struct cpufreq_pstates *pstates,
			  int count, char *str)
{
	int j;

	printf("%s@state\thits\t      total(us)\t\tavg(us)\tmin(us)\t"
	       "max(us)\n", str);
	for (j = 0; j < cstates->cstate_max + 1; j++) {
		struct cpuidle_cstate *c = &cstates->cstate[j];

		if (c->nrdata == 0)
			/* nothing to report for this state */
			continue;

		printf("%*c %s\t%d\t%15.2lf\t%15.2lf\t%.2lf\t%.2lf\n",
			(int)strlen(str), ' ',
			c->name, c->nrdata, c->duration,
			c->avg_time,
			(c->min_time == DBL_MAX ? 0. : c->min_time),
			c->max_time);
	}

	if (pstates) {
		for (j = 0; j < pstates->max; j++) {
			struct cpufreq_pstate *p = &(pstates->pstate[j]);

			if (p->count == 0)
				/* nothing to report for this state */
				continue;

			printf("%*c %d\t%d\t%15.2lf\t%15.2lf\t%.2lf\t%.2lf\n",
				(int)strlen(str), ' ',
				p->freq/1000, p->count, p->duration,
				p->avg_time,
				(p->min_time == DBL_MAX ? 0. : p->min_time),
				p->max_time);
		}
	}

	if (strstr(str, IRQ_WAKEUP_UNIT_NAME)) {
		struct wakeup_info *wakeinfo = &cstates->wakeinfo;
		struct wakeup_irq *irqinfo = wakeinfo->irqinfo;
		printf("%s wakeups \tname \t\tcount\n", str);
		for (j = 0; j < wakeinfo->nrdata; j++, irqinfo++) {
			printf("%*c %s%03d\t%-15.15s\t%d\n", (int)strlen(str),
				' ',
				(irqinfo->irq_type < IRQ_TYPE_MAX) ?
				irq_type_name[irqinfo->irq_type] : "NULL",
				irqinfo->id, irqinfo->name, irqinfo->count);
		}
	}

	return 0;
}

int dump_all_data(struct cpuidle_datas *datas, int count,
		int (*dump)(struct cpuidle_cstates *,
			    struct cpufreq_pstates *, int, char *))
{
	int i = 0, nrcpus = datas->nrcpus;
	struct cpuidle_cstates *cstates;
	struct cpufreq_pstates *pstates;

	do {
		cstates = &datas->cstates[i];
		pstates = &datas->pstates[i];

		if (nrcpus == -1)
			sprintf(buffer, "cluster");
		else
			sprintf(buffer, "cpu%d", i);

		dump(cstates, pstates, count, buffer);

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
	data->duration *= USEC_PER_SEC;

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
			struct cpuidle_data *tmp;

			/* intervals are ordered, no need to go further */
			if (c1->data[i].end < c2->data[j].begin)
				break;

			/* primary loop begins where we ended */
			if (c1->data[i].begin > c2->data[j].end)
				index = j;

			interval = intersection(&c1->data[i], &c2->data[j]);
			if (!interval)
				continue;

			result->min_time = MIN(result->min_time,
					       interval->duration);

			result->max_time = MAX(result->max_time,
					       interval->duration);

			result->avg_time = AVG(result->avg_time,
					       interval->duration,
					       result->nrdata + 1);

			result->duration += interval->duration;

			result->nrdata++;

			tmp = realloc(data, sizeof(*data) *
				       (result->nrdata + 1));
			if (!tmp) {
				free(data);
				free(result);
				return NULL;
			}
			data = tmp;

			result->data = data;
			result->data[result->nrdata - 1] = *interval;

			free(interval);
		}
	}

	return result;
}

#define CPUIDLE_STATENAME_PATH_FORMAT \
	"/sys/devices/system/cpu/cpu%d/cpuidle/state%d/name"

static char *cpuidle_cstate_name(int cpu, int state)
{
	char *fpath, *name;
	FILE *snf;
	char line[256];

	if (asprintf(&fpath, CPUIDLE_STATENAME_PATH_FORMAT, cpu, state) < 0)
		return NULL;

	/* read cpuidle state name for the CPU */
	snf = fopen(fpath, "r");
	free(fpath);
	if (!snf)
		/* file not found, or other error */
		return NULL;

	name = fgets(line, sizeof(line)/sizeof(line[0]), snf);
	fclose(snf);
	if (name) {
		/* get rid of trailing characters and duplicate string */
		name = strtok(name, "\n ");
		name = strdup(name);
	}
	return name;
}


/**
 * release_cstate_info - free all C-state related structs
 * @cstates: per-cpu array of C-state statistics structs
 * @nrcpus: number of CPUs
 */
static void release_cstate_info(struct cpuidle_cstates *cstates, int nrcpus)
{
	int cpu, i;

	if (!cstates)
		/* already cleaned up */
		return;

	/* free C-state names */
	for (cpu = 0; cpu < nrcpus; cpu++) {
		for (i = 0; i < MAXCSTATE; i++) {
			struct cpuidle_cstate *c = &(cstates[cpu].cstate[i]);
			if (c->name)
				free(c->name);
		}
	}

	/* free the cstates array */
	free(cstates);
}

/**
 * build_cstate_info - parse cpuidle sysfs entries and build per-CPU
 * structs to maintain statistics of C-state transitions
 * @nrcpus: number of CPUs
 *
 * Return: per-CPU array of structs (success) or NULL (error)
 */
static struct cpuidle_cstates *build_cstate_info(int nrcpus)
{
	int cpu;
	struct cpuidle_cstates *cstates;

	cstates = calloc(nrcpus, sizeof(*cstates));
	if (!cstates)
		return NULL;
	memset(cstates, 0, sizeof(*cstates) * nrcpus);

	/* initialize cstate_max for each cpu */
	for (cpu = 0; cpu < nrcpus; cpu++) {
		int i;
		struct cpuidle_cstate *c;
		cstates[cpu].cstate_max = -1;
		cstates[cpu].last_cstate = -1;
		for (i = 0; i < MAXCSTATE; i++) {
			c = &(cstates[cpu].cstate[i]);
			c->name = cpuidle_cstate_name(cpu, i);
			c->data = NULL;
			c->nrdata = 0;
			c->avg_time = 0.;
			c->max_time = 0.;
			c->min_time = DBL_MAX;
			c->duration = 0.;
		}
	}
	return cstates;
}

#define CPUFREQ_AVFREQ_PATH_FORMAT \
	"/sys/devices/system/cpu/cpu%d/cpufreq/scaling_available_frequencies"

/**
 * release_pstate_info - free all P-state related structs
 * @pstates: per-cpu array of P-state statistics structs
 * @nrcpus: number of CPUs
 */
static void release_pstate_info(struct cpufreq_pstates *pstates, int nrcpus)
{
	int cpu;

	if (!pstates)
		/* already cleaned up */
		return;

	/* first check and clean per-cpu structs */
	for (cpu = 0; cpu < nrcpus; cpu++)
		if (pstates[cpu].pstate)
			free(pstates[cpu].pstate);

	/* now free the master cpufreq structs */
	free(pstates);

	return;
}

/**
 * build_pstate_info - parse cpufreq sysfs entries and build per-CPU
 * structs to maintain statistics of P-state transitions
 * @nrcpus: number of CPUs
 *
 * Return: per-CPU array of structs (success) or NULL (error)
 */
static struct cpufreq_pstates *build_pstate_info(int nrcpus)
{
	int cpu;
	struct cpufreq_pstates *pstates;

	pstates = calloc(nrcpus, sizeof(*pstates));
	if (!pstates)
		return NULL;
	memset(pstates, 0, sizeof(*pstates) * nrcpus);

	for (cpu = 0; cpu < nrcpus; cpu++) {
		struct cpufreq_pstate *pstate;
		int nrfreq;
		char *fpath, *freq, line[256];
		FILE *sc_av_freq;

		if (asprintf(&fpath, CPUFREQ_AVFREQ_PATH_FORMAT, cpu) < 0)
			goto clean_exit;

		/* read scaling_available_frequencies for the CPU */
		sc_av_freq = fopen(fpath, "r");
		free(fpath);
		if (!sc_av_freq) {
			fprintf(stderr, "warning: P-states not supported for "
				"CPU%d\n", cpu);
			continue;
		}
		freq = fgets(line, sizeof(line)/sizeof(line[0]), sc_av_freq);
		fclose(sc_av_freq);
		if (!freq) {
			/* unlikely to be here, but just in case... */
			fprintf(stderr, "warning: P-state info not found for "
				"CPU%d\n", cpu);
			continue;
		}

		/* tokenize line and populate each frequency */
		nrfreq = 0;
		pstate = NULL;
		while ((freq = strtok(freq, "\n ")) != NULL) {
			struct cpufreq_pstate *tmp = realloc(pstate, sizeof(*pstate) * (nrfreq+1));
			if (!tmp)
				goto clean_exit;
			pstate = tmp;

			/* initialize pstate record */
			pstate[nrfreq].id = nrfreq;
			pstate[nrfreq].freq = atol(freq);
			pstate[nrfreq].count = 0;
			pstate[nrfreq].min_time = DBL_MAX;
			pstate[nrfreq].max_time = 0.;
			pstate[nrfreq].avg_time = 0.;
			pstate[nrfreq].duration = 0.;
			nrfreq++;
			freq = NULL;
		}

		/* now populate cpufreq_pstates for this CPU */
		pstates[cpu].pstate = pstate;
		pstates[cpu].max = nrfreq;
		pstates[cpu].current = -1;	/* unknown */
		pstates[cpu].idle = -1;		/* unknown */
		pstates[cpu].time_enter = 0.;
		pstates[cpu].time_exit = 0.;
	}

	return pstates;

clean_exit:
	release_pstate_info(pstates, nrcpus);
	return NULL;
}

static int get_current_pstate(struct cpuidle_datas *datas, int cpu,
				struct cpufreq_pstates **pstates,
				struct cpufreq_pstate **pstate)
{
	struct cpufreq_pstates *ps;

	if (cpu < 0 || cpu > datas->nrcpus)
		return -2;

	ps = &(datas->pstates[cpu]);

	*pstate = (ps->current == -1 ? NULL : &(ps->pstate[ps->current]));
	*pstates = ps;

	/* return 1 if CPU is idle, otherwise return 0 */
	return ps->idle;
}

static int freq_to_pstate_index(struct cpufreq_pstates *ps, unsigned int freq)
{
	int i;

	/* find frequency in table of P-states */
	for (i = 0; i < ps->max && freq != ps->pstate[i].freq; i++)
		/* just search */;

	/* if not found, return -1 */
	return i >= ps->max ? -1 : ps->pstate[i].id;
}

static void open_current_pstate(struct cpufreq_pstates *ps, double time)
{
	ps->time_enter = time;
}

static void open_next_pstate(struct cpufreq_pstates *ps, int s, double time)
{
	ps->current = s;
	if (ps->idle) {
		fprintf(stderr, "warning: opening P-state on idle CPU\n");
		return;
	}
	open_current_pstate(ps, time);
}

static void close_current_pstate(struct cpufreq_pstates *ps, double time)
{
	int c = ps->current;
	struct cpufreq_pstate *p = &(ps->pstate[c]);
	double elapsed;

	if (ps->idle) {
		fprintf(stderr, "warning: closing P-state on idle CPU\n");
		return;
	}
	elapsed = (time - ps->time_enter) * USEC_PER_SEC;
	p->min_time = MIN(p->min_time, elapsed);
	p->max_time = MAX(p->max_time, elapsed);
	p->avg_time = AVG(p->avg_time, elapsed, p->count + 1);
	p->duration += elapsed;
	p->count++;
}

static void cpu_change_pstate(struct cpuidle_datas *datas, int cpu,
			      unsigned int freq, double time)
{
	struct cpufreq_pstates *ps;
	struct cpufreq_pstate *p;
	int cur, next;

	cur = get_current_pstate(datas, cpu, &ps, &p);
	next = freq_to_pstate_index(ps, freq);

	switch (cur) {
	case 1:
		/* if CPU is idle, update current state and leave
		 * stats unchanged
		 */
		ps->current = next;
		return;

	case -1:
		/* current pstate is -1, i.e. this is the first update */
		open_next_pstate(ps, next, time);
		return;

	case 0:
		/* running CPU, update all stats, but skip closing current
		 * state if it's the initial update for CPU
		 */
		if (p)
			close_current_pstate(ps, time);
		open_next_pstate(ps, next, time);
		return;

	default:
		fprintf(stderr, "illegal pstate %d for cpu %d, exiting.\n",
			cur, cpu);
		exit(-1);
	}
}

static void cpu_pstate_idle(struct cpuidle_datas *datas, int cpu, double time)
{
	struct cpufreq_pstates *ps = &(datas->pstates[cpu]);
	if (ps->current != -1)
		close_current_pstate(ps, time);
	ps->idle = 1;
}

static void cpu_pstate_running(struct cpuidle_datas *datas, int cpu,
			       double time)
{
	struct cpufreq_pstates *ps = &(datas->pstates[cpu]);
	ps->idle = 0;
	if (ps->current != -1)
		open_current_pstate(ps, time);
}

static int store_data(double time, int state, int cpu,
		      struct cpuidle_datas *datas, int count)
{
	struct cpuidle_cstates *cstates = &datas->cstates[cpu];
	struct cpufreq_pstate *pstate = datas->pstates[cpu].pstate;
	struct cpuidle_cstate *cstate;
	struct cpuidle_data *data, *tmp;
	int nrdata, last_cstate = cstates->last_cstate;

	/* ignore when we got a "closing" state first */
	if (state == -1 && cstates->cstate_max == -1)
		return 0;

	cstate = &cstates->cstate[state == -1 ? last_cstate : state];
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
		data->duration *= USEC_PER_SEC;

		cstate->min_time = MIN(cstate->min_time, data->duration);

		cstate->max_time = MAX(cstate->max_time, data->duration);


		cstate->avg_time = AVG(cstate->avg_time, data->duration,
				       cstate->nrdata + 1);

		cstate->duration += data->duration;

		cstate->nrdata++;

		/* need indication if CPU is idle or not */
		cstates->last_cstate = -1;

		/* update P-state stats if supported */
		if (pstate)
			cpu_pstate_running(datas, cpu, time);

		return 0;
	}

	tmp = realloc(data, sizeof(*data) * (nrdata + 1));
	if (!tmp) {
		free(data);
		return error("realloc data");
	}
	data = tmp;

	data[nrdata].begin = time;

	cstates->cstate[state].data = data;
	cstates->cstate_max = MAX(cstates->cstate_max, state);
	cstates->last_cstate = state;
	cstates->wakeirq = NULL;

	/* update P-state stats if supported */
	if (pstate)
		cpu_pstate_idle(datas, cpu, time);

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
		strncpy(irqinfo->name, irqname, sizeof(irqinfo->name));
		irqinfo->name[sizeof(irqinfo->name) - 1] = '\0';
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
#define TRACE_FORMAT "%*[^]]]%*[^0-9] %lf:%*[^=]=%u%*[^=]=%u"

static int get_wakeup_irq(struct cpuidle_datas *datas, char *buffer, int count)
{
	int cpu, irqid;
	char irqname[NAMELEN+1];

	if (strstr(buffer, "irq_handler_entry")) {
		assert(sscanf(buffer, TRACE_IRQ_FORMAT, &cpu, &irqid,
			      irqname) == 3);

		store_irq(cpu, irqid, irqname, datas, count, HARD_IRQ);
		return 0;
	}

	if (strstr(buffer, "ipi_handler_entry")) {
		assert(sscanf(buffer, TRACE_IPIIRQ_FORMAT, &cpu, &irqid,
			      irqname) == 3);

		store_irq(cpu, irqid, irqname, datas, count, IPI_IRQ);
		return 0;
	}

	return -1;
}

struct cpuidle_datas *idlestat_load(const char *path)
{
	FILE *f;
	unsigned int state = 0, freq = 0, cpu = 0, nrcpus = 0;
	double time, begin = 0, end = 0;
	size_t count = 0, start = 1;
	struct cpuidle_datas *datas;
	int ret;

	f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "%s: failed to open '%s': %m\n", __func__, path);
		return NULL;
	}

	/* version line */
	fgets(buffer, BUFSIZE, f);

	fgets(buffer, BUFSIZE, f);
	assert(sscanf(buffer, "cpus=%u", &nrcpus) == 1);

	if (!nrcpus) {
		fclose(f);
		return ptrerror("read error for 'cpus=' in trace file");
	}

	datas = malloc(sizeof(*datas));
	if (!datas) {
		fclose(f);
		return ptrerror("malloc datas");
	}

	datas->cstates = build_cstate_info(nrcpus);
	if (!datas->cstates) {
		free(datas);
		fclose(f);
		return ptrerror("build_cstate_info: out of memory");
	}

	datas->pstates = build_pstate_info(nrcpus);
	if (!datas->pstates) {
		free(datas->cstates);
		free(datas);
		fclose(f);
		return ptrerror("build_pstate_info: out of memory");
	}

	datas->nrcpus = nrcpus;

	fgets(buffer, BUFSIZE, f);

	/* read topology information */
	read_cpu_topo_info(f, buffer);

	do {
		if (strstr(buffer, "cpu_idle")) {
			assert(sscanf(buffer, TRACE_FORMAT, &time, &state,
				      &cpu) == 3);

			if (start) {
				begin = time;
				start = 0;
			}
			end = time;

			store_data(time, state, cpu, datas, count);
			count++;
			continue;
		} else if (strstr(buffer, "cpu_frequency")) {
			assert(sscanf(buffer, TRACE_FORMAT, &time, &freq,
				      &cpu) == 3);
			assert(datas->pstates[cpu].pstate != NULL);
			cpu_change_pstate(datas, cpu, freq, time);
			count++;
			continue;
		}

		ret = get_wakeup_irq(datas, buffer, count);
		count += (0 == ret) ? 1 : 0;

	} while (fgets(buffer, BUFSIZE, f));

	fclose(f);

	fprintf(stderr, "Log is %lf secs long with %zd events\n",
		end - begin, count);

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
	if (!result->cstates) {
		free(result);
		return NULL;
	}

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

		/* copy state names from the first cpu */
		cstates->name = strdup(datas->cstates[0].cstate[i].name);

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
		/* copy state name from first cpu */
		s_cpu = list_first_entry(&s_core->cpu_head, struct cpu_cpu,
				list_cpu);
		cstates->name = strdup(s_cpu->cstates->cstate[i].name);

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
		/* copy state name from first core */
		s_core = list_first_entry(&s_phy->core_head, struct cpu_core,
				list_core);
		cstates->name = strdup(s_core->cstates->cstate[i].name);

		result->cstate[i] = *cstates;
	}

	return result;
}

static void help(const char *cmd)
{
	fprintf(stderr,
		"\nUsage:\nTrace mode: %s --trace -f|--trace-file <filename>"
		" -t|--duration <seconds> "
		"[-z|--dump] [-i|--iterations <number>] [-d|--debug]\n",
		basename(cmd));
	fprintf(stderr,
		"\nReporting mode: %s --import -f|--trace-file <filename>"
		"[-z|--dump] [-i|--iterations <number>] [-d|--debug]\n",
		basename(cmd));
	fprintf(stderr,
		"\nExample:\n%s --trace -f /tmp/myoutput -t 30\n", basename(cmd));
	fprintf(stderr,
		"\nExample:\n%s --import -f /tmp/mytrace\n", basename(cmd));
}

static void version(const char *cmd)
{
	printf("%s version %s\n", basename(cmd), IDLESTAT_VERSION);
}

enum modes {
  TRACE=1,
  IMPORT
};

int getoptions(int argc, char *argv[], struct program_options *options)
{
	struct option long_options[] = {
		{ "trace",       no_argument,       &options->mode, TRACE },
		{ "import",      no_argument,       &options->mode, IMPORT },
		{ "debug",       no_argument,       NULL, 'd' },
		{ "energy-model-file",  required_argument, NULL, 'e' },
		{ "trace-file",  required_argument, NULL, 'f' },
		{ "help",        no_argument,       NULL, 'h' },
		{ "iterations",  required_argument, NULL, 'i' },
		{ "duration",    required_argument, NULL, 't' },
		{ "version",     no_argument,       NULL, 'V' },
		{ "verbose",     no_argument,       NULL, 'v' },
		{ "dump",        no_argument,       NULL, 'z' },
		{ 0, 0, 0, 0 }
	};
	int c;

	memset(options, 0, sizeof(*options));
	options->filename = NULL;

	while (1) {

		int optindex = 0;

		c = getopt_long(argc, argv, ":de:f:hi:t:Vvz",
				long_options, &optindex);
		if (c == -1)
			break;

		switch (c) {
		case 'd':
			options->debug = true;
			break;
		case 'e':
			options->energy_model_filename = optarg;
			break;
		case 'f':
			options->filename = optarg;
			break;
		case 'h':
			help(argv[0]);
			exit(0);
			break;
		case 'i':
			options->iterations = atoi(optarg);
			break;
		case 't':
			options->duration = atoi(optarg);
			break;
		case 'V':
			version(argv[0]);
			exit(0);
			break;
		case 'v':
			options->verbose = true;
			break;
		case 'z':
			options->dump = true;
			break;
		case 0:     /* getopt_long() set a variable, just keep going */
			break;
		case ':':   /* missing option argument */
			fprintf(stderr, "%s: option `-%c' requires an argument\n",
				basename(argv[0]), optopt);
			return -1;
		case '?':   /* invalid option */
		default:
			fprintf(stderr, "%s: Unknown option `-%c'.\n",
				basename(argv[0]), optopt);
			help(argv[0]);
			return -1;
		}
	}

	if (parse_energy_model(options->energy_model_filename, options->filename) < 0) {
		fprintf(stderr, "can't parse energy model file\n");
		return -1;
	}

	if (options->iterations < 0)
		fprintf(stderr, "dump values must be a positive value\n");

	if (options->mode <= 0) {
		fprintf(stderr, "select a mode: --trace or --import\n");
		return -1;
	}

	if (NULL == options->filename) {
		fprintf(stderr, "expected -f <trace filename>\n");
		return -1;
	}

	if (options->mode == TRACE) {
		if (options->duration <= 0) {
			fprintf(stderr, "expected -t <seconds>\n");
			return -1;
		}
	}

	return optind;
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
		fprintf(stderr, "%s: failed to open '%s': %m\n", __func__, path);
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
		fprintf(stderr, "%s: failed to open '%s': %m\n", __func__, path);
		return -1;
	}

	fprintf(f, "version = %s\n", IDLESTAT_VERSION);
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

static volatile sig_atomic_t sigalrm = 0;

static void sighandler(int sig)
{
	if (sig == SIGALRM)
		sigalrm = 1;
}

static int execute(int argc, char *argv[], char *const envp[],
		   struct program_options *options)
{
	pid_t pid;
	int status;

	/* Nothing to execute, just wait an amount of time */
	if (!argc)
		return sleep(options->duration);

	pid = fork();
	if (pid < 0) {
		perror("fork");
		return -1;
	}

	if (pid == 0 && execvpe(argv[0], argv, envp)) {
		/* Forked child */
		perror("execvpe");
		exit(1);
	}

	if (pid) {

		struct sigaction s = {
			.sa_handler = sighandler,
			.sa_flags = SA_RESETHAND,
		};

		sigaddset(&s.sa_mask, SIGALRM);
		sigaction(SIGALRM, &s, NULL);
		alarm(options->duration);
	again:
		if (waitpid(pid, &status, 0) < 0) {
			if (errno == EINTR && sigalrm)
				kill(pid, SIGTERM);
			goto again;
		}

		if (WIFEXITED(status) && !WEXITSTATUS(status)) {
			/*
			 * Cancel the timer in case the program
			 * finished before the timeout
			 */
			alarm(0);
			return 0;
		}

		if (WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM)
			return 0;
	}

	return -1;
}

int main(int argc, char *argv[], char *const envp[])
{
	struct cpuidle_datas *datas;
	struct cpuidle_datas *cluster;
	struct rusage rusage;
	int args;

	args = getoptions(argc, argv, &options);
	if (args <= 0)
		return 1;

	/* Tracing requires manipulation of some files only accessible to root */
	if ((options.mode == TRACE) && getuid()) {
		fprintf(stderr, "must be root to run traces\n");
		return -1;
	}

	/* init cpu topoinfo */
	init_cpu_topo_info();

	/* Acquisition time specified means we will get the traces */
	if ((options.mode == TRACE) || args < argc) {

		/* Read cpu topology info from sysfs */
		read_sysfs_cpu_topo();

		/* Stop tracing (just in case) */
		if (idlestat_trace_enable(false)) {
			fprintf(stderr, "idlestat requires kernel Ftrace and debugfs mounted on /sys/kernel/debug\n");
			return -1;
		}

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

		/* Execute the command or wait a specified delay */
		if (execute(argc - args, &argv[args], envp, &options))
			return -1;

		/* Wake up all cpus again to account for last idle state */
		if (idlestat_wake_all())
			return -1;

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
			dump_cpu_topo_info(options.iterations, dump_states);
		else
			dump_cpu_topo_info(options.iterations, display_states);
		if (options.energy_model_filename)
			calculate_energy_consumption();
	} else {
		cluster = cluster_data(datas);
		if (!cluster)
			return 1;

		if (options.dump > 0) {
			dump_all_data(datas, options.iterations, dump_states);
			dump_all_data(cluster, options.iterations, dump_states);
		} else {
			dump_all_data(datas,
				      options.iterations, display_states);
			dump_all_data(cluster,
				      options.iterations, display_states);
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
	release_pstate_info(datas->pstates, datas->nrcpus);
	release_cstate_info(datas->cstates, datas->nrcpus);
	free(datas);

	return 0;
}
