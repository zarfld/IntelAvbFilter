# Standalone Makefile for test_hw_state_machine.exe
# Builds user-mode integration test for Hardware Context Lifecycle State Machine (REQ-F-HWCTX-001)
# Tests IOCTL_AVB_GET_HW_STATE (code 37) and validates 4-state machine
#
# Usage:
#   nmake /f Makefile.mak          # Build test_hw_state_machine.exe
#   nmake /f Makefile.mak clean    # Remove build artifacts

TARGET = test_hw_state_machine.exe
OBJS = test_hw_state_machine.obj

CC = cl
CFLAGS = /W4 /WX /Zi /Od /D_CRT_SECURE_NO_WARNINGS
LDFLAGS = /DEBUG
LIBS = kernel32.lib user32.lib

all: $(TARGET)

$(TARGET): $(OBJS)
	link $(LDFLAGS) /OUT:$(TARGET) $(OBJS) $(LIBS)

.c.obj:
	$(CC) $(CFLAGS) /c $<

clean:
	del /Q $(OBJS) $(TARGET) *.pdb *.ilk 2>nul

.PHONY: all clean
