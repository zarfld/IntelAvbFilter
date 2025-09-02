# nmake -f tools\avb_test\hardware_investigation.mak
# Build Hardware Investigation Tool

!IFNDEF CFG
CFG=Debug
!ENDIF

!IFNDEF PLATFORM
PLATFORM=x64
!ENDIF

OUTDIR=build\tools\avb_test\$(PLATFORM)\$(CFG)
CC=cl
LD=link
CFLAGS=/nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DHARDWARE_INVESTIGATION_USERMODE /I.
LDFLAGS=/nologo /DEBUG
LIBS=kernel32.lib user32.lib advapi32.lib

all: dirs $(OUTDIR)\hardware_investigation_tool.exe

dirs:
	@if not exist $(OUTDIR) mkdir $(OUTDIR)

OBJS=$(OUTDIR)\hardware_investigation_tool.obj

$(OUTDIR)\hardware_investigation_tool.obj: tools\avb_test\hardware_investigation_tool.c
	$(CC) $(CFLAGS) /Fo$(OUTDIR)\ /c tools\avb_test\hardware_investigation_tool.c

$(OUTDIR)\hardware_investigation_tool.exe: $(OBJS)
	$(LD) $(LDFLAGS) /OUT:$(OUTDIR)\hardware_investigation_tool.exe $(OBJS) $(LIBS)

clean:
	@if exist "$(OUTDIR)\hardware_investigation_tool.*" del /Q "$(OUTDIR)\hardware_investigation_tool.*"

.PHONY: all dirs clean