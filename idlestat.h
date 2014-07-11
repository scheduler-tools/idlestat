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

struct cpuidle_data {
	double begin;
	double end;
	double duration;
};

struct cpuidle_cstate {
	char *name;
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

struct cpuidle_datas *idlestat_load(const char *);

struct pstate_energy_info {
	unsigned int speed;
	unsigned int cluster_power;
	unsigned int core_power;
	double max_core_duration;
};

struct cstate_energy_info {
	char cstate_name[NAMELEN];
	unsigned int cluster_idle_power;
	unsigned int core_idle_power;
	double cluster_duration;
};

struct wakeup_energy_info {
	unsigned int cluster_wakeup_energy;
	unsigned int core_wakeup_energy;
};

enum energy_file_parse_state {
uninitialized = 0,
parsed_cluster_info,
parsing_cap_states,
parsing_c_states
};

struct cluster_energy_info {
	unsigned int number_cap_states;
	unsigned int number_c_states;
	struct pstate_energy_info *p_energy;
	struct cstate_energy_info *c_energy;
	struct wakeup_energy_info wakeup_energy;
	enum energy_file_parse_state state;
};

#endif
