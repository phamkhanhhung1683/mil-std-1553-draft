CC = gcc

TARGETS = bc rt

CFLAGS = -Wall -I../include -I../src

LDFLAGS = -L../lib -lADT_L1_Linux_x86_64 -lADT_L0_Linux_x86_64 -lftd3xx -pthread
LDFLAGS += -Wl,-rpath,'$$ORIGIN/../lib'

COMMON_SRCS = ../src/thread_safe_data_packet_queue.c ../src/ADT_L2_NLINE_U1553_bc.c ../src/ADT_L2_NLINE_U1553_rt.c 

.PHONY: all clean

all : $(TARGETS)

bc:
	$(CC) $(CFLAGS) -o $@ bc.c $(COMMON_SRCS) $(LDFLAGS) $(LDLIBS)

rt:
	$(CC) $(CFLAGS) -o $@ rt.c $(COMMON_SRCS) $(LDFLAGS) $(LDLIBS)

clean :
	rm -f *~ core *.o $(TARGETS)
