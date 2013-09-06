CFLAGS?=-g -Wall
CC?=gcc

OBJS = idlestat.o trace.o utils.o

default: idlestat

idlestat: $(OBJS)
	$(CC) ${CFLAGS} $(OBJS) -lncurses -o $@

clean:
	rm -f $(OBJS) idlestat
