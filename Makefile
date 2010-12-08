CC=diet gcc
CFLAGS=-Wall -Os
all:	cpuinfo
cpuinfo:	cpuinfo.o jelopt.o jelist.o
clean:	
	rm -f *.o cpuinfo
