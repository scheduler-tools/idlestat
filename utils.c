/*
 *  utils.c
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
#include <stdio.h>
#undef _GNU_SOURCE
#include <stdlib.h>

#include "utils.h"

int write_int(const char *path, int val)
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

int read_int(const char *path, int *val)
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

int store_line(const char *line, void *data)
{
	FILE *f = data;

	/* ignore comment line */
	if (line[0] == '#')
		return 0;

	fprintf(f, "%s", line);

	return 0;
}

/*
 * This functions is a helper to read a specific file content and store
 * the content inside a variable pointer passed as parameter, the format
 * parameter gives the variable type to be read from the file.
 *
 * @path : directory path containing the file
 * @name : name of the file to be read
 * @format : the format of the format
 * @value : a pointer to a variable to store the content of the file
 * Returns 0 on success, -1 otherwise
 */
int file_read_value(const char *path, const char *name,
			const char *format, void *value)
{
	FILE *file;
	char *rpath;
	int ret;

	ret = asprintf(&rpath, "%s/%s", path, name);
	if (ret < 0)
		return ret;

	file = fopen(rpath, "r");
	if (!file) {
		ret = -1;
		goto out_free;
	}

	ret = fscanf(file, format, value) == EOF ? -1 : 0;

	fclose(file);
out_free:
	free(rpath);
	return ret;
}
