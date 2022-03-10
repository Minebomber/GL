CC ?= gcc
CFLAGS ?= -Wall -Werror -g
LDFLAGS ?= -lGL -lglfw -ldl -lm -lassimp
PROGRAM ?= main.out

BUILD_DIR ?= ./build
SRC_DIR ?= ./src

SRCS := $(shell find $(SRC) -name *.c)
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

$(BUILD_DIR)/$(PROGRAM): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.c.o: %.c
	$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	$(RM) -r $(BUILD_DIR)

.PHONY: run
run:
	$(BUILD_DIR)/$(PROGRAM)

-include $(DEPS)

MKDIR_P ?= mkdir -p