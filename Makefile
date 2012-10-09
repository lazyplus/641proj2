# Makefile for liso HTTP Server
# by Yu Su <ysu1@andrew.cmu.edu>

CFLAGS=-Wall -Werror

all: routed

utility: utility.c utility.h
	@gcc ${CFLAGS} utility.c -c -o utility.o

rd: rd.c rd.h
	@gcc ${CFLAGS} rd.c -c -o rd.o

routed: routed.c rd utility
	@gcc ${CFLAGS} routed.c rd.o utility.o -o routed

clean:
	@rm *.o routed
