/*
 *  topology.h
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
#ifndef __TOPOLOGY_H
#define __TOPOLOGY_H

#include "list.h"
#include "idlestat.h"

struct cpu_cpu {
	struct list_head list_cpu;
	int cpu_id;
	struct cpuidle_cstates *cstates;
	struct cpufreq_pstates *pstates;
};

struct cpu_core {
	struct list_head list_core;
	int core_id;
	struct list_head cpu_head;
	int cpu_num;
	bool is_ht;
	struct cpuidle_cstates *cstates;
};

struct cpu_physical {
	struct list_head list_physical;
	int physical_id;
	struct list_head core_head;
	int core_num;
	struct cpuidle_cstates *cstates;
};

struct cpu_topology {
	struct list_head physical_head;
	int physical_num;
};

extern int init_cpu_topo_info(void);
extern int read_cpu_topo_info(FILE *f, char *buf);
extern int read_sysfs_cpu_topo(void);
extern int release_cpu_topo_info(void);
extern int output_cpu_topo_info(FILE *f);
extern int establish_idledata_to_topo(struct cpuidle_datas *datas);
extern int release_cpu_topo_cstates(void);
extern int dump_cpu_topo_info(int count,
		int (*dump)(struct cpuidle_cstates *, struct cpufreq_pstates *,
			    int, char *));


extern struct cpuidle_cstates *core_cluster_data(struct cpu_core *s_core);
extern struct cpuidle_cstates *
	physical_cluster_data(struct cpu_physical *s_phy);

#endif
