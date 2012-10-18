# Makefile for liso HTTP Server
# by Yu Su <ysu1@andrew.cmu.edu>

CC              = gcc
LD              = gcc
CFLAGS          = -Wall -Werror

LDFLAGS         =
DEFS            =
LIB             = 

all: routed

utility: utility.c utility.h
	$(CC) $(DEFS) $(CFLAGS) utility.c -c -o utility.o

rd: rd.c rd.h
	$(CC) $(DEFS) $(CFLAGS) rd.c -c -o rd.o

routed: routed.c rd utility
	$(CC) $(DEFS) $(CFLAGS) routed.c -c -o routed.o
	$(LD) $(LDFLAGS) -o $@ rd.o utility.o routed.o $(LIB)

clean:
	rm *.o routed
