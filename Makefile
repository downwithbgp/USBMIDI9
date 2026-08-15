# USBMIDI9 portable core - Linux host build and tests.
#
# Only the portable core (core/) and its host tests are built here. The
# Classic Mac OS, OMS, and FreeMIDI layers are target-specific and are NOT
# compiled on Linux (they contain no implementation yet; see docs/).
#
# Targets:
#   make             build the portable core library and the test binary
#   make test        build and run the host tests
#   make test-sanitize  build and run the tests under ASan/UBSan
#   make clean       remove build artifacts
#
# Override the compiler with: make test CC=clang

CC      ?= cc
CFLAGS  ?= -std=c89 -Wall -Wextra -Wpedantic -Werror -O2 -I.

BUILD   := build
SAN     := build-san

CORE_SRCS := core/packets.c core/descriptors.c core/ports.c
TEST_SRCS := tests/test_main.c tests/test_packets.c tests/test_descriptors.c

CORE_OBJS := $(CORE_SRCS:%.c=$(BUILD)/%.o)
TEST_OBJS := $(TEST_SRCS:%.c=$(BUILD)/%.o)
SAN_CORE_OBJS := $(CORE_SRCS:%.c=$(SAN)/%.o)
SAN_TEST_OBJS := $(TEST_SRCS:%.c=$(SAN)/%.o)

.PHONY: all test test-sanitize clean

all: $(BUILD)/libusbmidi9.a $(BUILD)/test_usbmidi9

$(BUILD)/libusbmidi9.a: $(CORE_OBJS)
	ar rcs $@ $(CORE_OBJS)

$(BUILD)/test_usbmidi9: $(TEST_OBJS) $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) $(CORE_OBJS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

test: all
	$(BUILD)/test_usbmidi9

test-sanitize: $(SAN)/test_usbmidi9
	$(SAN)/test_usbmidi9

$(SAN)/test_usbmidi9: $(SAN_TEST_OBJS) $(SAN_CORE_OBJS)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -o $@ $(SAN_TEST_OBJS) $(SAN_CORE_OBJS)

$(SAN)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -c -o $@ $<

clean:
	rm -rf $(BUILD) $(SAN)
