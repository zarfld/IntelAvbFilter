# Capability Validation Test - Verify realistic hardware capability reporting
# Tests the "Hardware Capability Reality" requirement from copilot instructions

!IF "$(CFG)" == ""
CFG=Debug
!ENDIF

!IF "$(PLATFORM)" == ""
PLATFORM=x64
!ENDIF

TARGET = avb_capability_validation_test.exe
OUTDIR = ..\..\..\build\tools\avb_test\$(PLATFORM)\$(CFG)
SRCDIR = .

CC = cl
LINK = link

!IF "$(CFG)" == "Debug"
# Include paths: 3 levels up to repo root
CFLAGS = /I..\..\..\include /I..\..\..\external\intel_avb\lib /I..\..\..\intel-ethernet-regs\gen /D_WIN32_WINNT=0x0A00 /DWIN32 /DWIN64 /D_WIN64 /Zi /Od /MDd
LINKFLAGS = /DEBUG /SUBSYSTEM:CONSOLE
!ELSE
CFLAGS = /I..\..\..\include /I..\..\..\external\intel_avb\lib /I..\..\..\intel-ethernet-regs\gen /D_WIN32_WINNT=0x0A00 /DWIN32 /DWIN64 /D_WIN64 /O2 /MD
LINKFLAGS = /SUBSYSTEM:CONSOLE
!ENDIF

!IF "$(PLATFORM)" == "x86"
LINKFLAGS = $(LINKFLAGS) /MACHINE:X86
!ELSE
LINKFLAGS = $(LINKFLAGS) /MACHINE:X64
!ENDIF

SOURCES = avb_capability_validation_test_um.c

OBJECTS = $(OUTDIR)\avb_capability_validation_test_um.obj

all: $(OUTDIR) $(OUTDIR)\$(TARGET)

$(OUTDIR):
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"

$(OUTDIR)\$(TARGET): $(OBJECTS)
	$(LINK) $(LINKFLAGS) /OUT:$@ $(OBJECTS) kernel32.lib user32.lib

$(OUTDIR)\avb_capability_validation_test_um.obj: avb_capability_validation_test_um.c
	$(CC) $(CFLAGS) /Fo$@ /c avb_capability_validation_test_um.c

clean:
	@if exist "$(OUTDIR)\avb_capability_validation_test_um.obj" del "$(OUTDIR)\avb_capability_validation_test_um.obj"
	@if exist "$(OUTDIR)\$(TARGET)" del "$(OUTDIR)\$(TARGET)"
	@if exist "$(OUTDIR)\$(TARGET:.exe=.pdb)" del "$(OUTDIR)\$(TARGET:.exe=.pdb)"