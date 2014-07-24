/*
 *  topology.c
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
#define  _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include <assert.h>

#include "list.h"
#include "utils.h"
#include "topology.h"
#include "idlestat.h"

struct cpu_topology g_cpu_topo_list;

struct topology_info {
	int physical_id;
	int core_id;
	int cpu_id;
};

struct list_info {
	struct list_head hlist;
	int id;
};

struct list_head *check_exist_from_head(struct list_head *head, int id)
{
	struct list_head *tmp;

	list_for_each(tmp, head) {
		if (id == ((struct list_info *)tmp)->id)
			return tmp;
	}

	return NULL;
}

struct list_head *check_pos_from_head(struct list_head *head, int id)
{
	struct list_head *tmp;

	list_for_each(tmp, head) {
		if (id < ((struct list_info *)tmp)->id)
			break;
	}

	return tmp->prev;
}

int add_topo_info(struct cpu_topology *topo_list, struct topology_info *info)
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;
	struct cpu_cpu      *s_cpu = NULL;
	struct list_head    *ptr;

	/* add cpu physical info */
	ptr = check_exist_from_head(&topo_list->physical_head,
					info->physical_id);
	if (!ptr) {
		s_phy = calloc(sizeof(struct cpu_physical), 1);
		if (!s_phy)
			return -1;

		s_phy->core_num = 0;
		s_phy->physical_id = info->physical_id;
		INIT_LIST_HEAD(&s_phy->core_head);

		ptr = check_pos_from_head(&topo_list->physical_head,
						s_phy->physical_id);
		list_add(&s_phy->list_physical, ptr);
		topo_list->physical_num++;
	} else {
		s_phy = list_entry(ptr, struct cpu_physical,
						list_physical);
	}

	/* add cpu core info */
	ptr = check_exist_from_head(&s_phy->core_head, info->core_id);
	if (!ptr) {
		s_core = calloc(sizeof(struct cpu_core), 1);
		if (!s_core)
			return -1;

		s_core->cpu_num = 0;
		s_core->is_ht = false;
		s_core->core_id = info->core_id;
		INIT_LIST_HEAD(&s_core->cpu_head);

		ptr = check_pos_from_head(&s_phy->core_head,
						s_core->core_id);
		list_add(&s_core->list_core, ptr);
		s_phy->core_num++;

	} else {
		s_core = list_entry(ptr, struct cpu_core, list_core);
	}

	/* add cpu info */
	ptr = check_exist_from_head(&s_core->cpu_head, info->cpu_id);
	if (!ptr) {
		s_cpu = calloc(sizeof(struct cpu_cpu), 1);
		if (!s_cpu)
			return -1;

		s_cpu->cpu_id = info->cpu_id;

		ptr = check_pos_from_head(&s_core->cpu_head, s_cpu->cpu_id);
		list_add(&s_cpu->list_cpu, ptr);
		s_core->cpu_num++;
		if (s_core->cpu_num > 1)
			s_core->is_ht = true;
	}

	return 0;
}

void free_cpu_cpu_list(struct list_head *head)
{
	struct cpu_cpu *lcpu, *n;

	list_for_each_entry_safe(lcpu, n, head, list_cpu) {
		list_del(&lcpu->list_cpu);
		free(lcpu);
	}
}

void free_cpu_core_list(struct list_head *head)
{
	struct cpu_core *lcore, *n;

	list_for_each_entry_safe(lcore, n, head, list_core) {
		free_cpu_cpu_list(&lcore->cpu_head);
		list_del(&lcore->list_core);
		free(lcore);
	}
}

void free_cpu_topology(struct list_head *head)
{
	struct cpu_physical *lphysical, *n;

	list_for_each_entry_safe(lphysical, n, head, list_physical) {
		free_cpu_core_list(&lphysical->core_head);
		list_del(&lphysical->list_physical);
		free(lphysical);
	}
}

int output_topo_info(struct cpu_topology *topo_list)
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;
	struct cpu_cpu      *s_cpu;

	list_for_each_entry(s_phy, &topo_list->physical_head, list_physical) {
		printf("cluster%c:\n", s_phy->physical_id + 'A');
		list_for_each_entry(s_core, &s_phy->core_head, list_core) {
			printf("\tcore%d\n", s_core->core_id);
			list_for_each_entry(s_cpu, &s_core->cpu_head, list_cpu)
				printf("\t\tcpu%d\n", s_cpu->cpu_id);
		}
	}

	return 0;
}

int outfile_topo_info(FILE *f, struct cpu_topology *topo_list)
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;
	struct cpu_cpu      *s_cpu;

	list_for_each_entry(s_phy, &topo_list->physical_head, list_physical) {
		fprintf(f, "# cluster%c:\n", s_phy->physical_id + 'A');
		list_for_each_entry(s_core, &s_phy->core_head, list_core) {
			fprintf(f, "#\tcore%d\n", s_core->core_id);
			list_for_each_entry(s_cpu, &s_core->cpu_head, list_cpu)
				fprintf(f, "#\t\tcpu%d\n", s_cpu->cpu_id);
		}
	}

	return 0;
}

struct cpu_cpu *find_cpu_point(struct cpu_topology *topo_list, int cpuid)
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;
	struct cpu_cpu      *s_cpu;

	list_for_each_entry(s_phy, &topo_list->physical_head, list_physical)
		list_for_each_entry(s_core, &s_phy->core_head, list_core)
			list_for_each_entry(s_cpu, &s_core->cpu_head, list_cpu)
				if (s_cpu->cpu_id == cpuid)
					return s_cpu;

	return NULL;
}

static inline int read_topology_cb(char *path, struct topology_info *info)
{
	file_read_value(path, "core_id", "%d", &info->core_id);
	file_read_value(path, "physical_package_id", "%d", &info->physical_id);

	return 0;
}

typedef int (*folder_filter_t)(const char *name);

static int cpu_filter_cb(const char *name)
{
	/* let's ignore some directories in order to avoid to be
	 * pulled inside the sysfs circular symlinks mess/hell
	 * (choose the word which fit better)*/
	if (!strcmp(name, "cpuidle"))
		return 1;

	if (!strcmp(name, "cpufreq"))
		return 1;

	return 0;
}

/*
 * This function will browse the directory structure and build a
 * reflecting the content of the directory tree.
 *
 * @path   : the root node of the folder
 * @filter : a callback to filter out the directories
 * Returns 0 on success, -1 otherwise
 */
static int topo_folder_scan(char *path, folder_filter_t filter)
{
	DIR *dir, *dir_topology;
	char *basedir, *newpath;
	struct dirent dirent, *direntp;
	struct stat s;
	int ret = 0;

	dir = opendir(path);
	if (!dir) {
		printf("error: unable to open directory %s\n", path);
		return -1;
	}

	ret = asprintf(&basedir, "%s", path);
	if (ret < 0) {
		closedir(dir);
		return -1;
	}

	while (!readdir_r(dir, &dirent, &direntp)) {

		if (!direntp)
			break;

		if (direntp->d_name[0] == '.')
			continue;

		if (filter && filter(direntp->d_name))
			continue;

		if (!strstr(direntp->d_name, "cpu"))
			continue;

		ret = asprintf(&newpath, "%s/%s/%s", basedir,
				direntp->d_name, "topology");
		if (ret < 0)
			goto out_free_basedir;

		ret = stat(newpath, &s);
		if (ret)
			goto out_free_newpath;

		if (S_ISDIR(s.st_mode) || (S_ISLNK(s.st_mode))) {
			struct topology_info cpu_info;

			dir_topology = opendir(path);
			if (!dir_topology)
				continue;
			closedir(dir_topology);

			read_topology_cb(newpath, &cpu_info);
			assert(sscanf(direntp->d_name, "cpu%d",
				      &cpu_info.cpu_id) == 1);
			add_topo_info(&g_cpu_topo_list, &cpu_info);
		}

out_free_newpath:
		free(newpath);

		if (ret)
			break;
	}

out_free_basedir:
	free(basedir);

	closedir(dir);

	return ret;
}


int init_cpu_topo_info(void)
{
	INIT_LIST_HEAD(&g_cpu_topo_list.physical_head);
	g_cpu_topo_list.physical_num = 0;

	return 0;
}

int read_sysfs_cpu_topo(void)
{
	topo_folder_scan("/sys/devices/system/cpu", cpu_filter_cb);

	return 0;
}

int read_cpu_topo_info(FILE *f, char *buf)
{
	int ret = 0;
	struct topology_info cpu_info;
	bool is_ht = false;
	char pid;

	do {
		ret = sscanf(buf, "# cluster%c", &pid);
		if (!ret)
			break;

		cpu_info.physical_id = pid - 'A';

		fgets(buf, BUFSIZE, f);
		do {
			ret = sscanf(buf, "#\tcore%d", &cpu_info.core_id);
			if (ret) {
				is_ht = true;
				fgets(buf, BUFSIZE, f);
			} else {
				ret = sscanf(buf, "#\tcpu%d", &cpu_info.cpu_id);
				if (ret)
					is_ht = false;
				else
					break;
			}

			do {
				if (!is_ht) {
					ret = sscanf(buf, "#\tcpu%d",
						     &cpu_info.cpu_id);
					cpu_info.core_id = cpu_info.cpu_id;
				} else {
					ret = sscanf(buf, "#\t\tcpu%d",
						     &cpu_info.cpu_id);
				}

				if (!ret)
					break;

				add_topo_info(&g_cpu_topo_list, &cpu_info);

				fgets(buf, BUFSIZE, f);
			} while (1);
		} while (1);
	} while (1);

	/* output_topo_info(&g_cpu_topo_list); */

	return 0;
}

int release_cpu_topo_info(void)
{
	/* free alloced memory */
	free_cpu_topology(&g_cpu_topo_list.physical_head);

	return 0;
}

int output_cpu_topo_info(FILE *f)
{
	outfile_topo_info(f, &g_cpu_topo_list);

	return 0;
}

int establish_idledata_to_topo(struct cpuidle_datas *datas)
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;
	struct cpu_cpu      *s_cpu;
	int    i;
	int    has_topo = 0;

	for (i = 0; i < datas->nrcpus; i++) {
		s_cpu = find_cpu_point(&g_cpu_topo_list, i);
		if (s_cpu) {
			s_cpu->cstates = &datas->cstates[i];
			s_cpu->pstates = &datas->pstates[i];
			has_topo = 1;
		}
	}

	if (!has_topo)
		return -1;

	list_for_each_entry(s_phy, &g_cpu_topo_list.physical_head,
			    list_physical)
		list_for_each_entry(s_core, &s_phy->core_head, list_core)
			s_core->cstates = core_cluster_data(datas, s_core);

	list_for_each_entry(s_phy, &g_cpu_topo_list.physical_head,
			    list_physical)
		s_phy->cstates = physical_cluster_data(datas, s_phy);

	return 0;
}

int dump_cpu_topo_info(int count,
	int (*dump)(struct cpuidle_cstates *, struct cpufreq_pstates *,
	int, char *))
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;
	struct cpu_cpu      *s_cpu;
	char   tmp[30];
	int    tab = 0;

	list_for_each_entry(s_phy, &g_cpu_topo_list.physical_head,
			    list_physical) {
		sprintf(tmp, "cluster%c", s_phy->physical_id + 'A');
		dump(s_phy->cstates, NULL, count, tmp);

		list_for_each_entry(s_core, &s_phy->core_head, list_core) {
			if (s_core->is_ht) {
				sprintf(tmp, "  core%d", s_core->core_id);
				dump(s_core->cstates, NULL, count, tmp);

				tab = 1;
			} else {
				tab = 0;
			}

			list_for_each_entry(s_cpu, &s_core->cpu_head,
					    list_cpu) {
				sprintf(tmp, "%*ccpu%d", (tab + 1) * 2, 0x20,
					s_cpu->cpu_id);
				dump(s_cpu->cstates, s_cpu->pstates,
				     count, tmp);
			}
		}
	}

	return 0;
}

int release_cpu_topo_cstates(void)
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;

	list_for_each_entry(s_phy, &g_cpu_topo_list.physical_head,
			    list_physical) {
		free(s_phy->cstates);
		s_phy->cstates = NULL;
		list_for_each_entry(s_core, &s_phy->core_head, list_core)
			if (s_core->is_ht) {
				free(s_core->cstates);
				s_core->cstates = NULL;
			}
	}

	return 0;
}
