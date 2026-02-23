# SOURCE is the C program to build (with the Layer 0 and Layer 1 shared objects)
SOURCE=draft1.c thread_safe_buf_queue.c

# TARGET is the executable output
TARGET=draft1

all : 
	gcc -Wall -o $(TARGET) $(SOURCE)

clean :
	rm -f *~ core *.o $(TARGET)

