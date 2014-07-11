#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "idlestat.h"
#include "topology.h"
#include "list.h"

static char buffer[BUFSIZE];

static struct cluster_energy_info *cluster_energy_table;
static unsigned int clusters_in_energy_file = 0;

int parse_energy_model(const char *path)
{
	FILE *f;
	char tmp;
	struct cluster_energy_info *clustp;
	unsigned int number_cap_states, number_c_states;
	int current_cluster = -1;
	unsigned int current_pstate;
	unsigned int current_cstate;
	unsigned int clust_p, core_p;

	f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "%s: failed to open '%s': %m\n", __func__, path);
		return -1;
	}

	fgets(buffer, BUFSIZE, f);

	do {
		if (buffer[0] == '#') continue;
		if (strlen(buffer) == 1) continue;

		if (strstr(buffer, "clusters")) {
			if (clusters_in_energy_file) {
				fprintf(stderr, "%s: number of clusters already specified in %s\n", __func__, path);
				return -1;
			}
			sscanf(buffer, "%*s %d", &clusters_in_energy_file);
			cluster_energy_table = calloc(sizeof(struct cluster_energy_info), clusters_in_energy_file);
			continue;
		}
		if (strstr(buffer, "cluster")) {
			sscanf(buffer, "cluster%c: %d cap states %d C states", &tmp, &number_cap_states, &number_c_states);
			current_cluster = tmp - 'A';
			if (current_cluster >= clusters_in_energy_file) {
				fprintf(stderr, "%s: cluster%c out of range in %s\n", __func__, tmp, path);
				return -1;
			}
			clustp = cluster_energy_table + current_cluster;
			if (clustp->number_cap_states) {
				fprintf(stderr, "%s: number of cap states for cluster%c already specified in %s\n", __func__, tmp, path);
				return -1;
			}
			clustp->number_cap_states = number_cap_states;
			clustp->number_c_states = number_c_states;
			clustp->p_energy = calloc(number_cap_states, sizeof(struct pstate_energy_info));
			clustp->c_energy = calloc(number_c_states, sizeof(struct cstate_energy_info));
			clustp->state = parsed_cluster_info;
			continue;
		}
		if (strstr(buffer, "P-states")) {
			if (current_cluster == -1) {
				fprintf(stderr, "%s: unknown cluster (cap states) in %s\n", __func__, path);
				return -1;
			}
			if (clustp->state < parsed_cluster_info) {
				fprintf(stderr, "%s: number of cap states for cluster%c not specified in %s\n", __func__, current_cluster, path);
				return -1;
			}
			current_pstate = 0;
			clustp->state = parsing_cap_states;
			continue;
		}
		if (strstr(buffer, "C-states")) {
			if (current_cluster == -1) {
				fprintf(stderr, "%s: unknown cluster (c states) in %s\n", __func__, path);
				return -1;
			}
			if (clustp->state < parsed_cluster_info) {
				fprintf(stderr, "%s: number of c states for cluster%c not specified in %s\n", __func__, current_cluster, path);
				return -1;
			}
			current_cstate = 0;
			clustp->state = parsing_c_states;
			continue;
		}
		if (strstr(buffer, "wakeup")) {
			unsigned int clust_w, core_w;

			if (current_cluster == -1) {
				fprintf(stderr, "%s: unknown cluster (wakeup) in %s\n", __func__, path);
				return -1;
			}
			sscanf(buffer, "%*s %d %d", &clust_w, &core_w);
			clustp->wakeup_energy.cluster_wakeup_energy = clust_w;
			clustp->wakeup_energy.core_wakeup_energy = core_w;
			continue;
		}
		if (!clustp) {
			fprintf(stderr, "%s: unknown cluster in %s\n", __func__, path);
			return -1;
			}
		if (clustp->state == parsing_cap_states) {
			struct pstate_energy_info *pp;
			unsigned int speed;

			if (sscanf(buffer, "%d %d %d", &speed, &clust_p, &core_p) != 3) {
				fprintf(stderr, "%s: expected P state (speed cluster core) for cluster%c in %s\n", __func__, current_cluster, path);
				return -1;
			}
			
			if (current_pstate >= clustp->number_cap_states) {
				fprintf(stderr, "%s: too many cap states specified for cluster%c in %s\n", __func__, current_cluster, path);
				return -1;
			}
			pp = &clustp->p_energy[current_pstate++];
			pp->speed = speed;
			pp->cluster_power = clust_p;
			pp->core_power = core_p;
			continue;
		}
		if (clustp->state == parsing_c_states) {
			char name[NAMELEN];
			struct cstate_energy_info *cp;

			if (sscanf(buffer, "%s %d %d", name, &clust_p, &core_p) != 3) {
				fprintf(stderr, "%s: expected C state (name cluster core) for cluster%c in %s\n", __func__, current_cluster, path);
				return -1;
			}

			if (current_cstate >= clustp->number_c_states) {
				fprintf(stderr, "%s: too many C states specified for cluster%c in %s\n", __func__, current_cluster, path);
				return -1;
			}
			cp = &clustp->c_energy[current_cstate++];
			strncpy(cp->cstate_name, name, NAMELEN);
			cp->cluster_idle_power = clust_p;
			cp->core_idle_power = core_p;
			continue;
		}
	} while (fgets(buffer, BUFSIZE, f));

	printf("parsed energy model file\n");
	return 0;
}

static struct cstate_energy_info *find_cstate_energy_info(const unsigned int cluster, const char *name)
{
	struct cluster_energy_info *clustp;
	struct cstate_energy_info *cp;
	int i;

	clustp = cluster_energy_table + cluster;
	cp = &clustp->c_energy[0];
	for (i = 0; i < clustp->number_c_states; i++, cp++) {
		if (!strcmp(cp->cstate_name, name)) return cp;
	}
	return NULL;
}

static struct pstate_energy_info *find_pstate_energy_info(const unsigned int cluster, const unsigned int speed)
{
	struct cluster_energy_info *clustp;
	struct pstate_energy_info *pp;
	int i;

	clustp = cluster_energy_table + cluster;
	pp = &clustp->p_energy[0];
	for (i = 0; i < clustp->number_cap_states; i++, pp++) {
		if (speed == pp->speed) return pp;
	}
	return NULL;
}

void calculate_energy_consumption(void)
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;
	struct cpu_cpu      *s_cpu;
	double total_energy_used = 0.0;
	double energy_from_cap_states = 0.0;
	double energy_from_idle = 0.0;
	double energy_from_wakeups = 0.0;
	int i, j;
	unsigned int current_cluster;
	struct cstate_energy_info *cp;
	struct pstate_energy_info *pp;
	unsigned int cluster_cstate_count;
	struct cluster_energy_info *clustp;

	list_for_each_entry(s_phy, &g_cpu_topo_list.physical_head,
			    list_physical) {
		current_cluster = s_phy->physical_id;
		cluster_cstate_count = 0;
		clustp = cluster_energy_table + current_cluster;
		for (j = 0; j < s_phy->cstates->cstate_max + 1; j++) {
			struct cpuidle_cstate *c = &s_phy->cstates->cstate[j];

			if (c->nrdata == 0)
				continue;
			cp = find_cstate_energy_info(current_cluster, c->name);
			if (!cp)
				continue;
			cluster_cstate_count += c->nrdata;
			cp->cluster_duration = c->duration;
			energy_from_idle += c->duration * cp->cluster_idle_power;
		}
		energy_from_wakeups += (1 << 10) * cluster_cstate_count * clustp->wakeup_energy.cluster_wakeup_energy;
		list_for_each_entry(s_core, &s_phy->core_head, list_core) {
			list_for_each_entry(s_cpu, &s_core->cpu_head,
					    list_cpu) {
				for (i = 0; i < s_cpu->cstates->cstate_max + 1; i++) {
					struct cpuidle_cstate *c = &s_cpu->cstates->cstate[i];
					if (c->nrdata == 0)
						continue;
					cp = find_cstate_energy_info(current_cluster, c->name);
					if (!cp)
						continue;
					energy_from_idle += (c->duration - cp->cluster_duration) * cp->core_idle_power;
				}
				for (i = 0; i < s_cpu->pstates->max; i++) {
					struct cpufreq_pstate *p = &s_cpu->pstates->pstate[i];

					if (p->count == 0)
						continue;
					pp = find_pstate_energy_info(current_cluster, p->freq/1000);
					if (!pp)
						continue;
					pp->max_core_duration = MAX(p->duration, pp->max_core_duration);
					energy_from_cap_states += p->duration * pp->core_power;
				}
			}
		}
		/*
		 * XXX
		 * No cluster P-state duration info available yet, so estimate this
		 * as the maximum of the durations of its cores at that frequency.
		 */
		for (i = 0; i < clustp->number_cap_states; i++) {
			pp = &clustp->p_energy[i];
			energy_from_cap_states += pp->max_core_duration * pp->cluster_power;
		}
	}
	printf("\n");
	printf("energy consumption from cap states \t%e\n", energy_from_cap_states);
	printf("energy consumption from idle \t\t%e\n", energy_from_idle);
	printf("energy consumption from wakeups \t%e\n", energy_from_wakeups);
	total_energy_used = energy_from_cap_states + energy_from_idle + energy_from_wakeups;
	printf("total energy consumption estimate \t%e\n", total_energy_used);
}
