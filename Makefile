CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L

.PHONY: all test clean

all: mini-htop

mini-htop: c_src/main.c c_src/monitor.c c_src/monitor.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ c_src/main.c c_src/monitor.c

test-monitor: tests/test_monitor.c c_src/monitor.c c_src/monitor.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -I c_src -o $@ tests/test_monitor.c c_src/monitor.c

demo-load: demo/cpu_load.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ demo/cpu_load.c

test: mini-htop test-monitor
	./test-monitor
	./mini-htop --once

clean:
	rm -f mini-htop test-monitor demo-load
