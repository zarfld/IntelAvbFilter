# Makefile for Registry Diagnostics Integration Test
# Tests: Issue #17 (REQ-NF-DIAG-REG-001)

TARGET = test_registry_diagnostics.exe
SOURCES = test_registry_diagnostics.c
OBJECTS = $(SOURCES:.c=.obj)

# Compiler settings
CC = cl.exe
CFLAGS = /nologo /W4 /WX /Zi /Od /MD /DUNICODE /D_UNICODE
INCLUDES = /I"../../../include"
LIBS = kernel32.lib advapi32.lib

# Build rules
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) /Fe$(TARGET) $(OBJECTS) $(LIBS)

.c.obj:
	$(CC) $(CFLAGS) $(INCLUDES) /c $<

clean:
	del /Q $(OBJECTS) $(TARGET) *.pdb *.ilk 2>nul

rebuild: clean all

.PHONY: all clean rebuild
