PLATFORM ?= posix

SRC_DIR := src
INCLUDE_DIR := include
BUILD_DIR := build

CC := clang
CFLAGS := -Wall -Wextra -Wpedantic -std=c99 -I$(INCLUDE_DIR)

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS)) $(BUILD_DIR)/pal/pal_$(PLATFORM).o
TARGET := $(BUILD_DIR)/anedota

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

clean: 
	rm -rf $(BUILD_DIR)
