#define _GNU_SOURCE
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sched.h>
#include <string.h>
#include <values.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/resource.h>

#define BUFSIZE 256
#define MAXCSTATE 8
#define MAX(A,B) (A > B ? A : B)
#define MIN(A,B) (A < B ? A : B)
#define AVG(A,B,I) ((A) + ((B - A) / (I)))

static char buffer[BUFSIZE];

struct cpuidle_data {
	double begin;
	double end;
	double duration;
};

struct cpuidle_cstate {
	struct cpuidle_data *data;
	int nrdata;
	double avg_time;
	double max_time;
	double min_time;
	double duration;
};

struct cpuidle_cstates {
	struct cpuidle_cstate cstate[MAXCSTATE];
	int last_cstate;
	int cstate_max;
};

struct cpuidle_datas {
	struct cpuidle_cstates *cstates;
	int nrcpus;
};

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

static int dump_data(struct cpuidle_datas *datas, int state, int count)
{
	int i = 0, j, k, nrcpus = datas->nrcpus;
	struct cpuidle_cstates *cstates;
	struct cpuidle_cstate *cstate;

	do {
		cstates = &datas->cstates[i];

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

		i++;

	} while (i < nrcpus && nrcpus != -1);

	return 0;
}

static int display_data(struct cpuidle_datas *datas, int state)
{
	int i = 0, j, nrcpus = datas->nrcpus;
	struct cpuidle_cstates *cstates;
	struct cpuidle_cstate *cstate;

	do {
		cstates = &datas->cstates[i];

		for (j = 0; j < cstates->cstate_max + 1; j++) {

			if (state != -1 && state != j)
				continue;

			cstate = &cstates->cstate[j];

			if (nrcpus == -1)
				printf("cluster");
			else
				printf("cpu%d", i);

			printf("/state%d, %d hits, total %.2lfus, "\
			       "avg %.2lfus, min %.2lfus, max %.2lfus\n",
			       j, cstate->nrdata, cstate->duration,
			       cstate->avg_time, cstate->min_time,
			       cstate->max_time);
		}

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

	return 0;
}

#define TRACE_CMD_FORMAT "%*[^]]] %lf:%*[^=]=%u%*[^=]=%d"
#define TRACE_FORMAT "%*[^]]] %*s %lf:%*[^=]=%u%*[^=]=%d"

static struct cpuidle_datas *idlestat_load(const char *path)
{
	FILE *f;
	unsigned int state = 0, cpu = 0, nrcpus= 0;
	double time, begin, end;
	size_t count, start;
	struct cpuidle_datas *datas;

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

	/* TODO: read topology information */

	for (start = 1; fgets(buffer, BUFSIZE, f); count++) {

		if (!strstr(buffer, "cpu_idle"))
			continue;

		sscanf(buffer, TRACE_FORMAT, &time, &state, &cpu);

		if (start) {
			begin = time;
			start = 0;
		}
		end = time;

		store_data(time, state, cpu, datas, count);
	}

	fclose(f);

	fprintf(stderr, "Log is %lf secs long with %d events\n",
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

static int help(const char *cmd)
{
	fprintf(stderr, "%s [-d/--dump] [-c/--cstate=x] <file>\n", cmd);
	exit(0);
}

static struct option long_options[] = {
	{ "dump",       0, 0, 'd' },
	{ "iterations", 0, 0, 'i' },
	{ "cstate",     0, 0, 'c' },
	{ "debug",      0, 0, 'g' },
	{ "verbose",    0, 0, 'v' },
	{ "help",       0, 0, 'h' },
	{ 0,            0, 0, 0   }
};

struct idledebug_options {
	bool debug;
	bool dump;
	int cstate;
	int iterations;
	unsigned int duration;
};

int getoptions(int argc, char *argv[], struct idledebug_options *options)
{
	int c;

	memset(options, 0, sizeof(*options));
	options->cstate = -1;

	while (1) {

		int optindex = 0;

		c = getopt_long(argc, argv, "gdvhi:c:t:",
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
		case 'h':
			help(argv[0]);
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

	if (optind == argc) {
		fprintf(stderr, "expected filename\n");
		return -1;
	}

	return 0;
}

#define TRACE_PATH "/sys/kernel/debug/tracing"
#define TRACE_ON_PATH TRACE_PATH "/tracing_on"
#define TRACE_BUFFER_SIZE_PATH TRACE_PATH "/buffer_size_kb"
#define TRACE_BUFFER_TOTAL_PATH TRACE_PATH "/buffer_total_size_kb"
#define TRACE_CPUIDLE_EVENT_PATH TRACE_PATH "/events/power/cpu_idle/enable"
#define TRACE_EVENT_PATH TRACE_PATH "/events/enable"
#define TRACE_FREE TRACE_PATH "/free_buffer"
#define TRACE_FILE TRACE_PATH "/trace"
#define TRACE_IDLE_NRHITS_PER_SEC 10000
#define TRACE_IDLE_LENGTH 196

static int write_int(const char *path, int val)
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

static int read_int(const char *path, int *val)
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

static int idlestat_trace_enable(bool enable)
{
	return write_int(TRACE_ON_PATH, enable);
}

static int idlestat_flush_trace(void)
{
	return write_int(TRACE_FILE, 0);
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

static int store_line(const char *line, void *data)
{
	FILE *f = data;

	/* ignore comment line */
	if (line[0] == '#')
		return 0;

	fprintf(f, "%s", line);

	return 0;
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

	/* TODO: add topology information here */

	ret = idlestat_file_for_each_line(TRACE_FILE, f, store_line);

	fclose(f);

	return ret;
}

static int idlestat_init_trace(unsigned int duration)
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

	/* We have to manipulate some files only accessible to root */
	if (getuid()) {
		fprintf(stderr, "must be root to run the tool\n");
		return -1;
	}

	if (getoptions(argc, argv, &options))
		return 1;

	/* Acquisition time specified means we will get the traces */
	if (options.duration) {

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
		if (idlestat_store(argv[optind]))
			return -1;
	}

	/* Load the idle states information */
	datas = idlestat_load(argv[optind]);
	if (!datas)
		return 1;

	/* Compute cluster idle intersection between cpus belonging to
	 * the same cluster
	 * TODO: add topology information
	 */
	cluster = cluster_data(datas);
	if (!cluster)
		return 1;

	if (options.dump > 0) {
		dump_data(datas, options.cstate, options.iterations);
		dump_data(cluster, options.cstate, options.iterations);
	} else {
		display_data(datas, options.cstate);
		display_data(cluster, options.cstate);
	}

	/* Computation could be heavy, let's give some information
	 * about the memory consumption */
	if (options.debug) {
		getrusage(RUSAGE_SELF, &rusage);
		printf("max rss : %ld kB\n", rusage.ru_maxrss);
	}

	return 0;
}
