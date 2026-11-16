CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Werror -pthread -D_GNU_SOURCE
LDFLAGS ?= -pthread

CORE_SRCS = src/protocol.c src/config.c src/hid_linux.c src/sensors.c src/udp_server.c
CORE_OBJS = $(patsubst src/%.c,build/%.o,$(CORE_SRCS))

.PHONY: all clean test

all: build/system-temperature-monitoringd build/stmctl

build:
	mkdir -p build

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/system-temperature-monitoringd: $(CORE_OBJS) build/main.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

build/stmctl: $(CORE_OBJS) build/stmctl.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

build/test_protocol: build/protocol.o build/config.o tests/test_protocol.c | build
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: build/test_protocol
	./build/test_protocol

clean:
	rm -rf build
