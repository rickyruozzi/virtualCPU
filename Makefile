CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -pedantic
THREAD_FLAGS ?= -pthread

CPU_TARGET := VirtualCPU.o
DEMO_TARGET := VirtualCPUDemo.exe

.PHONY: all v t clean

all: v

v: $(CPU_TARGET)

t: $(DEMO_TARGET)

$(CPU_TARGET): VirtualCPU.c VirtualCPU.h
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -c VirtualCPU.c -o $(CPU_TARGET)

$(DEMO_TARGET): VirtualCPU.c VirtualCPU.h VirtualCPUDemo.c
	$(CC) $(CFLAGS) $(THREAD_FLAGS) VirtualCPU.c VirtualCPUDemo.c -o $(DEMO_TARGET)

clean:
	$(RM) $(CPU_TARGET) $(DEMO_TARGET)
