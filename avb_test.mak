# Makefile for AVB Test Application
# Usage: nmake -f avb_test.mak

TARGET = avb_test.exe
SOURCE = avb_test.c

CC = cl.exe
CFLAGS = /nologo /W4 /O2
LDFLAGS = /nologo
LIBS = 

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) $(LDFLAGS) $(LIBS) /Fe:$(TARGET)

clean:
	del /Q $(TARGET) *.obj *.pdb 2>nul

.PHONY: all clean
