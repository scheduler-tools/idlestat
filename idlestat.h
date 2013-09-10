
#ifndef __IDLESTAT_H
#define __IDLESTAT_H

#define BUFSIZE 256
#define MAXCSTATE 8
#define MAX(A, B) (A > B ? A : B)
#define MIN(A, B) (A < B ? A : B)
#define AVG(A, B, I) ((A) + ((B - A) / (I)))

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

#endif
