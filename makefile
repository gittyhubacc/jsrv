CC := gcc
CFLAGS := -g -O0
LDFLAGS := -luring -lpthread

BIN_NAME := jsrv

SRC_DIR := ./src
OBJ_DIR := ./obj
BIN_DIR := ./bin

SRCS := $(shell find $(SRC_DIR) -name '*.c')
OBJS := $(SRCS:%.c=$(OBJ_DIR)/%.o)

all: $(BIN_DIR)/$(BIN_NAME)

$(BIN_DIR)/$(BIN_NAME): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@ 

.PHONY: clean test
clean:
	rm -rf $(BIN_DIR) $(OBJ_DIR)
test: $(BIN_DIR)/$(BIN_NAME)
	valgrind ./$(BIN_DIR)/$(BIN_NAME)
