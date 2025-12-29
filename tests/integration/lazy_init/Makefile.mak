# Makefile for Lazy Initialization Integration Test
# Tests: Issue #16 (REQ-F-LAZY-INIT-001)

TARGET = test_lazy_initialization.exe
SOURCES = test_lazy_initialization.c
OBJECTS = $(SOURCES:.c=.obj)

# Compiler settings
CC = cl.exe
CFLAGS = /nologo /W4 /WX /Zi /Od /MD /DUNICODE /D_UNICODE
INCLUDES = /I"../../../include"
LIBS = kernel32.lib

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
