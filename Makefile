# USBMIDI9 portable core - Linux host build and tests.
#
# The portable core (core/) and the ring buffer (classic/ring.c) are built
# and host-tested here. The Classic driver and probe sources are NOT built
# into the Linux binaries; they are compile-checked against minimal stub
# headers (classic/host-check/) by `make check-classic`. OMS and FreeMIDI
# layers contain no implementation yet (see docs/).
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

CORE_SRCS := core/packets.c core/descriptors.c core/ports.c core/midi_stream.c
RING_SRCS := classic/ring.c
TEST_SRCS := tests/test_main.c tests/test_packets.c tests/test_descriptors.c tests/test_ring.c tests/test_machine.c tests/test_probe.c tests/test_midi_stream.c tests/test_oms_driver.c

CORE_OBJS := $(CORE_SRCS:%.c=$(BUILD)/%.o)
RING_OBJS := $(RING_SRCS:%.c=$(BUILD)/%.o)
TEST_OBJS := $(TEST_SRCS:%.c=$(BUILD)/%.o)
SAN_CORE_OBJS := $(CORE_SRCS:%.c=$(SAN)/%.o)
SAN_RING_OBJS := $(RING_SRCS:%.c=$(SAN)/%.o)
SAN_TEST_OBJS := $(TEST_SRCS:%.c=$(SAN)/%.o)

.PHONY: all test test-sanitize check-classic clean

all: $(BUILD)/libusbmidi9.a $(BUILD)/test_usbmidi9

$(BUILD)/libusbmidi9.a: $(CORE_OBJS)
	ar rcs $@ $(CORE_OBJS)

$(BUILD)/test_usbmidi9: $(TEST_OBJS) $(CORE_OBJS) $(RING_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) $(CORE_OBJS) $(RING_OBJS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# The state-machine test compiles the Classic driver source against the
# stub headers with a mock USL, so it uses the Classic flags (no
# -Wpedantic; stub headers legitimately cast -1 to a function pointer
# for kUSBNoCallBack). It includes classic/usb_driver.c, so it depends
# on the driver sources and the stub headers too.
MACHINE_DEPS := tests/test_machine.c classic/usb_driver.c classic/usb_driver.h \
                classic/usbmidi9_dispatch.h classic/ring.h \
                classic/host-check/*.h

$(BUILD)/tests/test_machine.o: $(MACHINE_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CLASSIC_CFLAGS) -c -o $@ $<

$(SAN)/tests/test_machine.o: $(MACHINE_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CLASSIC_CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -c -o $@ $<

# The probe test compiles probe/probe.c (via #include) against the stub
# headers with the Classic flags, so it uses the same dependency
# tracking and -Iclassic (the probe includes "usbmidi9_dispatch.h",
# resolved through the classic access path, mirroring the G4 project).
PROBE_DEPS := tests/test_probe.c probe/probe.c classic/usbmidi9_dispatch.h \
              classic/host-check/*.h

$(BUILD)/tests/test_probe.o: $(PROBE_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CLASSIC_CFLAGS) -c -o $@ $<

$(SAN)/tests/test_probe.o: $(PROBE_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CLASSIC_CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -c -o $@ $<

# The OMS shim test compiles oms/oms_driver.c + oms/oms_rx.c + oms/oms_tx.c
# (via #include) against the stub headers with a mock OMS/USB environment,
# so it uses the Classic flags and depends on the shim sources, the OMS
# stub headers, and the neutral stream converter. It links against
# core/midi_stream.o (the converter the shim consumes).
OMS_DEPS := tests/test_oms_driver.c oms/oms_driver.c oms/oms_rx.c oms/oms_tx.c \
            oms/oms_driver.h core/midi_stream.h core/midi_stream.c \
            classic/usbmidi9_dispatch.h classic/host-check/*.h

$(BUILD)/tests/test_oms_driver.o: $(OMS_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CLASSIC_CFLAGS) -c -o $@ $<

$(SAN)/tests/test_oms_driver.o: $(OMS_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CLASSIC_CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -c -o $@ $<

test: all
	$(BUILD)/test_usbmidi9

test-sanitize: $(SAN)/test_usbmidi9
	$(SAN)/test_usbmidi9

$(SAN)/test_usbmidi9: $(SAN_TEST_OBJS) $(SAN_CORE_OBJS) $(SAN_RING_OBJS)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -o $@ $(SAN_TEST_OBJS) $(SAN_CORE_OBJS) $(SAN_RING_OBJS)

$(SAN)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -c -o $@ $<

# Syntax/type check of the Classic Mac OS sources against minimal stub
# headers (classic/host-check/). The real build happens in CodeWarrior on
# the Power Mac G4; this only catches C-level errors on Linux.
# -Wdeclaration-after-statement enforces C89 block layout.
CLASSIC_CFLAGS := -std=c89 -Wall -Wextra -Werror -Wdeclaration-after-statement \
                  -I. -Iclassic/host-check -Iclassic

check-classic:
	$(CC) $(CLASSIC_CFLAGS) -c -o $(BUILD)/usb_driver.o classic/usb_driver.c
	$(CC) $(CLASSIC_CFLAGS) -c -o $(BUILD)/probe.o probe/probe.c
	# The OMS driver entry is `long main(short, long, long)` (OMSDriver.h);
	# -Wmain only accepts the C signature, so the host check renames it
	# (the G4 build exports the real `main` via USBMIDI9_OMS.exp).
	$(CC) $(CLASSIC_CFLAGS) -Dmain=oms_driver_entry -c -o $(BUILD)/oms_driver.o oms/oms_driver.c
	@echo "check-classic: Classic sources compile against stub headers"

clean:
	rm -rf $(BUILD) $(SAN)
