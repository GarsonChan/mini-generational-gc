#
CC = gcc
SRCS = generationalgc.c
BIN = generationalgc.h

all: clean gc

clean:
	rm -f gc

gc: $(SRCS)
	$(CC) -g -o gc $(SRCS)

test: clean gc
	./gc test