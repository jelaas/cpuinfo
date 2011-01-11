CC=diet gcc
CFLAGS=-Wall -Os -D_GNU_SOURCE -DVERSION=\"1.1\"
all:	cpuinfo
cpuinfo:	cpuinfo.o jelopt.o jelist.o
clean:	
	rm -f *.o cpuinfo
