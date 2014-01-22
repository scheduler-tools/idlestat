#
# Makefile
#
# Copyright (C) 2014 Zoran Markovic <zoran.markovic@linaro.org>
#
# This file is subject to the terms and conditions of the GNU General Public
# License version 2.
#
CFLAGS?=-g -Wall
CC?=gcc

OBJS = idlestat.o topology.o trace.o utils.o

default: idlestat

idlestat: $(OBJS)
	$(CC) ${CFLAGS} $(OBJS) -o $@

clean:
	rm -f $(OBJS) idlestat
