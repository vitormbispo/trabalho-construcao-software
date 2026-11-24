PLATFORM ?= posix

SRC_DIR := src
INCLUDE_DIR := include
BUILD_DIR := build
TEST_DIR := tests
EXAMPLE_DIR := examples

CC := clang
AR := ar
CFLAGS := -Wall -Wextra -Wpedantic -std=c99 -I$(INCLUDE_DIR)

ALL_SRCS := $(wildcard $(SRC_DIR)/*.c)
CLI_SRCS := $(SRC_DIR)/main.c $(wildcard $(SRC_DIR)/command_*.c)
CORE_SRCS := $(filter-out $(CLI_SRCS),$(ALL_SRCS))

CORE_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CORE_SRCS)) $(BUILD_DIR)/pal/pal_$(PLATFORM).o
CLI_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CLI_SRCS))

LIB_TARGET := $(BUILD_DIR)/libanedota.a
TARGET := $(BUILD_DIR)/anedota
EXAMPLE_TARGET := $(BUILD_DIR)/examples/host_app

TEST_SRCS := $(wildcard $(TEST_DIR)/*.c)
TEST_BINS := $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/tests/%,$(TEST_SRCS))

.PHONY: all lib examples clean test

all: $(LIB_TARGET) $(TARGET) $(EXAMPLE_TARGET)

lib: $(LIB_TARGET)

examples: $(EXAMPLE_TARGET)

$(LIB_TARGET): $(CORE_OBJS) | $(BUILD_DIR)
	$(AR) rcs $@ $^

$(TARGET): $(CLI_OBJS) $(LIB_TARGET)
	$(CC) $(CFLAGS) -o $@ $^

$(EXAMPLE_TARGET): $(EXAMPLE_DIR)/host_app.c $(LIB_TARGET) | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/tests/test_command_init: $(TEST_DIR)/test_command_init.c $(BUILD_DIR)/command_init.o $(LIB_TARGET) | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/tests/%: $(TEST_DIR)/%.c $(LIB_TARGET) | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^

test: $(LIB_TARGET) $(TARGET) $(EXAMPLE_TARGET) $(TEST_BINS)
	@for t in $(TEST_BINS); do \
		./$$t || exit 1; \
	done
	./$(EXAMPLE_TARGET)
	sh $(TEST_DIR)/test_cli.sh "$(abspath $(TARGET))"

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
