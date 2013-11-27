
#ifndef __IDLESTAT_H
#define __IDLESTAT_H

#define BUFSIZE 256
#define NAMELEN 16
#define MAXCSTATE 8
#define MAX(A, B) (A > B ? A : B)
#define MIN(A, B) (A < B ? A : B)
#define AVG(A, B, I) ((A) + ((B - A) / (I)))

#define IRQ_WAKEUP_UNIT_NAME "cpu"

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

enum IRQ_TYPE {
	HARD_IRQ = 0,
	IPI_IRQ,
	IRQ_TYPE_MAX
};

struct wakeup_irq {
	int id;
	int irq_type;
	char name[NAMELEN+1];
	int count;
};

struct wakeup_info {
	struct wakeup_irq *irqinfo;
	int nrdata;
};

struct cpuidle_cstates {
	struct cpuidle_cstate cstate[MAXCSTATE];
	struct wakeup_info wakeinfo;
	int last_cstate;
	int cstate_max;
	struct wakeup_irq *wakeirq;
};

struct cpuidle_datas {
	struct cpuidle_cstates *cstates;
	int nrcpus;
};

#endif
