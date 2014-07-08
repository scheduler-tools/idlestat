/*
 *  idlestat.h
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
#ifndef __IDLESTAT_H
#define __IDLESTAT_H

#define BUFSIZE 256
#define NAMELEN 16
#define MAXCSTATE 16
#define MAXPSTATE 16
#define MAX(A, B) (A > B ? A : B)
#define MIN(A, B) (A < B ? A : B)
#define AVG(A, B, I) ((A) + ((B - A) / (I)))

#define IRQ_WAKEUP_UNIT_NAME "cpu"

#define CPUIDLE_STATE_TARGETRESIDENCY_PATH_FORMAT \
	"/sys/devices/system/cpu/cpu%d/cpuidle/state%d/residency"
#define CPUFREQ_AVFREQ_PATH_FORMAT \
	"/sys/devices/system/cpu/cpu%d/cpufreq/scaling_available_frequencies"
#define CPUIDLE_STATENAME_PATH_FORMAT \
	"/sys/devices/system/cpu/cpu%d/cpuidle/state%d/name"

struct cpuidle_data {
	double begin;
	double end;
	double duration;
};

struct cpuidle_cstate {
	char *name;
	struct cpuidle_data *data;
	int nrdata;
	int premature_wakeup;
	int could_sleep_more;
	double avg_time;
	double max_time;
	double min_time;
	double duration;
	int target_residency; /* -1 if not available */
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
	int not_predicted;
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
	int not_predicted;
};

struct cpufreq_pstate {
	int id;
	unsigned int freq;
	int count;
	double min_time;
	double max_time;
	double avg_time;
	double duration;
};

struct cpufreq_pstates {
	struct cpufreq_pstate *pstate;
	int current;
	int idle;
	double time_enter;
	double time_exit;
	int max;
};

struct cpuidle_datas {
	struct cpuidle_cstates *cstates;
	struct cpufreq_pstates *pstates;
	int nrcpus;
};

#endif
