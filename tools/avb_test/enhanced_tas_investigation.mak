# nmake -f tools\avb_test\enhanced_tas_investigation.mak
# Build Enhanced TAS Investigation Tool

!IFNDEF CFG
CFG=Debug
!ENDIF

!IFNDEF PLATFORM
PLATFORM=x64
!ENDIF

OUTDIR=..\..\build\tools\avb_test\$(PLATFORM)\$(CFG)
CC=cl
LD=link
CFLAGS=/nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DENHANCED_TAS_INVESTIGATION /I. /I..\..\external\intel_avb\include
LDFLAGS=/nologo /DEBUG
LIBS=kernel32.lib user32.lib advapi32.lib

all: dirs $(OUTDIR)\enhanced_tas_investigation.exe

dirs:
	@if not exist $(OUTDIR) mkdir $(OUTDIR)

OBJS=$(OUTDIR)\enhanced_tas_investigation.obj

$(OUTDIR)\enhanced_tas_investigation.obj: enhanced_tas_investigation.c
	$(CC) $(CFLAGS) /Fo$(OUTDIR)\ /c enhanced_tas_investigation.c

$(OUTDIR)\enhanced_tas_investigation.exe: $(OBJS)
	$(LD) $(LDFLAGS) /OUT:$(OUTDIR)\enhanced_tas_investigation.exe $(OBJS) $(LIBS)

clean:
	@if exist "$(OUTDIR)\enhanced_tas_investigation.*" del /Q "$(OUTDIR)\enhanced_tas_investigation.*"

.PHONY: all dirs clean