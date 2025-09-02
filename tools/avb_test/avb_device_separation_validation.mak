# Device Separation Validation Test - Verify clean architecture compliance
# Tests that generic layer doesn't contain device-specific register access

TARGET = avb_device_separation_test.exe
OUTDIR = ..\..\build\tools\avb_test\x64\Debug
SRCDIR = .

CC = cl
LINK = link

# Fix: Use x64 platform consistently
CFLAGS = /I..\..\external\intel_avb\include /D_WIN32_WINNT=0x0A00 /DWIN32 /DWIN64 /D_WIN64 /Zi /Od /MDd
LINKFLAGS = /DEBUG /SUBSYSTEM:CONSOLE /MACHINE:X64

SOURCES = avb_device_separation_test_um.c

OBJECTS = $(OUTDIR)\avb_device_separation_test_um.obj

all: $(OUTDIR) $(OUTDIR)\$(TARGET)

$(OUTDIR):
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"

$(OUTDIR)\$(TARGET): $(OBJECTS)
	$(LINK) $(LINKFLAGS) /OUT:$@ $(OBJECTS) kernel32.lib user32.lib

$(OUTDIR)\avb_device_separation_test_um.obj: avb_device_separation_test_um.c
	$(CC) $(CFLAGS) /Fo$@ /c avb_device_separation_test_um.c

clean:
	@if exist "$(OUTDIR)\avb_device_separation_test_um.obj" del "$(OUTDIR)\avb_device_separation_test_um.obj"
	@if exist "$(OUTDIR)\$(TARGET)" del "$(OUTDIR)\$(TARGET)"
	@if exist "$(OUTDIR)\$(TARGET:.exe=.pdb)" del "$(OUTDIR)\$(TARGET:.exe=.pdb)"