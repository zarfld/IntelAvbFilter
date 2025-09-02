# nmake -f tools\avb_test\critical_prerequisites_investigation.mak
# Build Critical Prerequisites Investigation Tool

!IFNDEF CFG
CFG=Debug
!ENDIF

!IFNDEF PLATFORM
PLATFORM=x64
!ENDIF

OUTDIR=build\tools\avb_test\$(PLATFORM)\$(CFG)
CC=cl
LD=link
CFLAGS=/nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DCRITICAL_INVESTIGATION /I.
LDFLAGS=/nologo /DEBUG
LIBS=kernel32.lib user32.lib advapi32.lib

all: dirs $(OUTDIR)\critical_prerequisites_investigation.exe

dirs:
	@if not exist $(OUTDIR) mkdir $(OUTDIR)

OBJS=$(OUTDIR)\critical_prerequisites_investigation.obj

$(OUTDIR)\critical_prerequisites_investigation.obj: tools\avb_test\critical_prerequisites_investigation.c
	$(CC) $(CFLAGS) /Fo$(OUTDIR)\ /c tools\avb_test\critical_prerequisites_investigation.c

$(OUTDIR)\critical_prerequisites_investigation.exe: $(OBJS)
	$(LD) $(LDFLAGS) /OUT:$(OUTDIR)\critical_prerequisites_investigation.exe $(OBJS) $(LIBS)

clean:
	@if exist "$(OUTDIR)\critical_prerequisites_investigation.*" del /Q "$(OUTDIR)\critical_prerequisites_investigation.*"

.PHONY: all dirs clean