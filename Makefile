CFLAGS?=-g -Wall
CC?=gcc

OBJS = idlestat.o

default: idlestat

idledebug: $(OBJS)
	$(CC) ${CFLAGS} $(OBJS) -lncurses -o $@

clean:
	rm -f $(OBJS) idlestat
