PLATFORM ?= posix

SRC_DIR := src
INCLUDE_DIR := include
BUILD_DIR := build
TEST_DIR := tests

CC := clang
CFLAGS := -Wall -Wextra -Wpedantic -std=c99 -I$(INCLUDE_DIR)

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS)) $(BUILD_DIR)/pal/pal_$(PLATFORM).o
LIB_OBJS := $(filter-out $(BUILD_DIR)/main.o,$(OBJS))
TARGET := $(BUILD_DIR)/anedota

TEST_SRCS := $(wildcard $(TEST_DIR)/*.c)
TEST_BINS := $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/tests/%,$(TEST_SRCS))

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/tests/%: $(TEST_DIR)/%.c $(LIB_OBJS) | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
		./$$t || exit 1; \
	done

$(BUILD_DIR):
	mkdir -p $@

clean: 
	rm -rf $(BUILD_DIR)
