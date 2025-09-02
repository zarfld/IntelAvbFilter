# Capability Validation Test - Verify realistic hardware capability reporting
# Tests the "Hardware Capability Reality" requirement from copilot instructions

TARGET = avb_capability_validation_test.exe
OUTDIR = ..\..\build\tools\avb_test\x64\Debug
SRCDIR = .

CC = cl
LINK = link

CFLAGS = /I..\..\external\intel_avb\include /D_WIN32_WINNT=0x0A00 /DWIN32 /D_X86_=1 /Zi /Od /MDd
LINKFLAGS = /DEBUG /SUBSYSTEM:CONSOLE /MACHINE:X64

SOURCES = avb_capability_validation_test_um.c

OBJECTS = $(OUTDIR)\avb_capability_validation_test_um.obj

all: $(OUTDIR) $(OUTDIR)\$(TARGET)

$(OUTDIR):
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"

$(OUTDIR)\$(TARGET): $(OBJECTS)
	$(LINK) $(LINKFLAGS) /OUT:$@ $(OBJECTS) kernel32.lib user32.lib

$(OUTDIR)\avb_capability_validation_test_um.obj: avb_capability_validation_test_um.c
	$(CC) $(CFLAGS) /Fo$@ /c $<

clean:
	@if exist "$(OUTDIR)\avb_capability_validation_test_um.obj" del "$(OUTDIR)\avb_capability_validation_test_um.obj"
	@if exist "$(OUTDIR)\$(TARGET)" del "$(OUTDIR)\$(TARGET)"
	@if exist "$(OUTDIR)\$(TARGET:.exe=.pdb)" del "$(OUTDIR)\$(TARGET:.exe=.pdb)"