# Makefile for test_ndis_receive_path.exe
# User-mode integration test for NDIS FilterReceive implementation
# Verifies: #43 (REQ-F-NDIS-RECEIVE-001: FilterReceive / FilterReceiveNetBufferLists)
# Traces to: #290 (TEST-NDIS-RECEIVE-PATH-001)

# Compiler and linker
CC = cl.exe
LINK = link.exe

# Output executable
TARGET = test_ndis_receive_path.exe

# Source files
SOURCES = test_ndis_receive_path.c

# Include directories
INCLUDES = /I..\..\..\include /I..\..\..\external\intel_avb\lib /I..\..\..\intel-ethernet-regs\gen

# Compiler flags
CFLAGS = /nologo /W4 /WX /Zi /Od /MD $(INCLUDES) /D_CONSOLE /D_UNICODE /DUNICODE

# Linker flags
LDFLAGS = /nologo /DEBUG /SUBSYSTEM:CONSOLE

# Libraries
LIBS = kernel32.lib

# Build target
all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) /link $(LDFLAGS) $(LIBS) /OUT:$(TARGET)
	@echo.
	@echo ============================================================
	@echo   Build Complete: $(TARGET)
	@echo ============================================================
	@echo   Test File: tests/integration/ndis_receive/test_ndis_receive_path.c
	@echo   Verifies: #43 (REQ-F-NDIS-RECEIVE-001)
	@echo   Traces to: #290 (TEST-NDIS-RECEIVE-PATH-001)
	@echo ============================================================
	@echo.

clean:
	@if exist $(TARGET) del /Q $(TARGET)
	@if exist *.obj del /Q *.obj
	@if exist *.pdb del /Q *.pdb
	@if exist *.ilk del /Q *.ilk
	@echo Cleaned build artifacts

run: $(TARGET)
	@echo.
	@echo ============================================================
	@echo   Running NDIS FilterReceive Tests
	@echo ============================================================
	@.\$(TARGET)
