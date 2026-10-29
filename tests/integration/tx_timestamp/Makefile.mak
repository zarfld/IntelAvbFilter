# Standalone Makefile for TX Timestamp Retrieval Test
# Implements: Issue #35 (REQ-F-IOCTL-TS-001)
#
# Build using: nmake /f Makefile.mak
# Run using: test_tx_timestamp_retrieval.exe

TARGET = test_tx_timestamp_retrieval.exe
OBJS = test_tx_timestamp_retrieval.obj

# Compiler and flags
CC = cl.exe
CFLAGS = /nologo /W4 /WX /O2 /Zi /I..\..\..\include

# Linker and flags
LINK = link.exe
LDFLAGS = /nologo /DEBUG /SUBSYSTEM:CONSOLE

# Default target
all: $(TARGET)

# Build executable
$(TARGET): $(OBJS)
	$(LINK) $(LDFLAGS) /OUT:$(TARGET) $(OBJS)

# Compile source
test_tx_timestamp_retrieval.obj: test_tx_timestamp_retrieval.c
	$(CC) $(CFLAGS) /c test_tx_timestamp_retrieval.c

# Clean build artifacts
clean:
	@if exist $(TARGET) del /Q $(TARGET)
	@if exist *.obj del /Q *.obj
	@if exist *.pdb del /Q *.pdb
	@if exist *.ilk del /Q *.ilk

# Run test: the registered entry through the authoritative dispatcher (build provenance, HIL safety gate, adapter
# state check; TEST-HILPATH-014).  It runs the Build-Tests artifact, never the local $(TARGET) of this makefile.
!IFNDEF AVB_RUN_CFG
AVB_RUN_CFG = Debug
!ENDIF
run:
	powershell -NoProfile -ExecutionPolicy Bypass -File ..\..\..\tools\test\Run-Tests-CI.ps1 -Suite HardwareUnit -Configuration $(AVB_RUN_CFG) -Tests test_tx_timestamp_retrieval

.PHONY: all clean run
