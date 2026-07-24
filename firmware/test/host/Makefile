# Host-side unit tests for the pure-logic firmware modules
# (damper.c, failsafe.c, battery.c — zero ESP-IDF includes).
#
#   make        # build all test binaries
#   make test   # build + run; non-zero exit if any suite fails
#   make clean
#
# Framework: vendored ThrowTheSwitch Unity v2.6.0 (unity.c/.h,
# unity_internals.h in this directory).

CC      ?= gcc
MAIN     = ../../main
BUILD    = build

# Project code is held to a stricter standard than the vendored framework.
CFLAGS        = -std=c11 -g -O1 -I. -I$(MAIN)
STRICT_FLAGS  = -Wall -Wextra -Werror
UNITY_FLAGS   = -Wall

TESTS = test_damper test_failsafe test_battery
BINS  = $(addprefix $(BUILD)/,$(TESTS))

.PHONY: all test clean

all: $(BINS)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/unity.o: unity.c unity.h unity_internals.h | $(BUILD)
	$(CC) $(CFLAGS) $(UNITY_FLAGS) -c $< -o $@

$(BUILD)/damper.o: $(MAIN)/damper.c $(MAIN)/damper.h | $(BUILD)
	$(CC) $(CFLAGS) $(STRICT_FLAGS) -c $< -o $@

$(BUILD)/failsafe.o: $(MAIN)/failsafe.c $(MAIN)/failsafe.h | $(BUILD)
	$(CC) $(CFLAGS) $(STRICT_FLAGS) -c $< -o $@

$(BUILD)/battery.o: $(MAIN)/battery.c $(MAIN)/battery.h | $(BUILD)
	$(CC) $(CFLAGS) $(STRICT_FLAGS) -c $< -o $@

$(BUILD)/test_%.o: test_%.c unity.h | $(BUILD)
	$(CC) $(CFLAGS) $(STRICT_FLAGS) -c $< -o $@

$(BUILD)/test_damper: $(BUILD)/test_damper.o $(BUILD)/damper.o $(BUILD)/unity.o
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/test_failsafe: $(BUILD)/test_failsafe.o $(BUILD)/failsafe.o $(BUILD)/unity.o
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/test_battery: $(BUILD)/test_battery.o $(BUILD)/battery.o $(BUILD)/unity.o
	$(CC) $(CFLAGS) -o $@ $^

test: $(BINS)
	@rc=0; \
	for t in $(BINS); do \
		echo "==== $$t ===="; \
		./$$t || rc=1; \
		echo; \
	done; \
	exit $$rc

clean:
	rm -rf $(BUILD)
