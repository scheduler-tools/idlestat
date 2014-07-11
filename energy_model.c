#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "idlestat.h"

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
