# Makefile for Windows + MSVC

# Compiler & Linker
CC=cl
LINK=link

# Flags
CFLAGS=/nologo /W3 /O2 /EHsc /std:c17
DEBUGFLAGS=/nologo /W3 /Od /EHsc /std:c17 /Zi
LDFLAGS=/nologo

# Directories
SRC_DIR=src
BUILD_DIR=build
OBJ_DIR=$(BUILD_DIR)/obj
BIN_DIR=$(BUILD_DIR)/bin

# Files
SRCS=$(wildcard $(SRC_DIR)/*.c)
OBJS=$(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.obj,$(SRCS))
TARGET=$(BIN_DIR)/program.exe

# Default target
all: release

# Release
release: CFLAGS=$(CFLAGS)
release: $(TARGET)

# Debug
debug: CFLAGS=$(DEBUGFLAGS)
debug: $(TARGET)

# Link executable
$(TARGET): $(OBJS)
	@echo Linking $@
	@if not exist $(BIN_DIR) mkdir $(BIN_DIR)
	$(LINK) $(OBJS) /OUT:$@ $(LDFLAGS)

# Compile objects
$(OBJ_DIR)/%.obj: $(SRC_DIR)/%.c
	@echo Compiling $<
	@if not exist $(OBJ_DIR) mkdir $(OBJ_DIR)
	$(CC) $(CFLAGS) /c $< /Fo$@

# Clean
clean:
	@echo Cleaning...
	if exist $(OBJ_DIR) rmdir /s /q $(OBJ_DIR)
	if exist $(BIN_DIR) rmdir /s /q $(BIN_DIR)
