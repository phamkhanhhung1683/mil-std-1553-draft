# SOURCE is the C program to build (with the Layer 0 and Layer 1 shared objects)
SOURCES=rt.c thread_safe_buf_queue.c

# TARGET is the executable output
TARGET=test_bc

all : 
	gcc -Wall -o $(TARGET) $(SOURCES) -L. libADT_L1_Linux_x86_64.so libADT_L0_Linux_x86_64.so libftd3xx.so

clean :
	rm -f *~ core *.o $(TARGET)

