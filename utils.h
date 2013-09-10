
#ifndef __UTILS_H
#define __UTILS_H

extern int write_int(const char *path, int val);
extern int read_int(const char *path, int *val);
extern int store_line(const char *line, void *data);
extern int file_read_value(const char *path, const char *name,
				const char *format, void *value);

#endif
