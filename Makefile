# Makefile for FTP Server

CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -pthread
DEBUG_FLAGS = -g -DDEBUG
LDFLAGS = -pthread

# Directories
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin

# Files
TARGET = $(BIN_DIR)/ftpserver
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))
DEPS = $(wildcard $(INC_DIR)/*.h)

# Phony targets
.PHONY: all clean debug dirs

all: dirs $(TARGET)

debug: CFLAGS += $(DEBUG_FLAGS)
debug: all

dirs:
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(DEPS)
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

clean:
	rm -rf $(BIN_DIR) $(OBJ_DIR)

# Installation targets
.PHONY: install uninstall

install: all
	@echo "Installing ftpserver to /usr/local/bin..."
	@install -m 755 $(TARGET) /usr/local/bin/
	@mkdir -p /var/log/ftpserver
	@chmod 755 /var/log/ftpserver
	@echo "Installation complete."

uninstall:
	@echo "Uninstalling ftpserver..."
	@rm -f /usr/local/bin/ftpserver
	@echo "Uninstallation complete."
