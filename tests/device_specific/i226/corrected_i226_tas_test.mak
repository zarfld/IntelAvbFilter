# nmake -f corrected_i226_tas_test.mak
# Build Corrected I226 TAS Test Tool

!IFNDEF CFG
CFG=Debug
!ENDIF

!IFNDEF PLATFORM
PLATFORM=x64
!ENDIF

OUTDIR=..\..\..\build\tools\avb_test\$(PLATFORM)\$(CFG)
CC=cl
LD=link
CFLAGS=/nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DCORRECTED_I226_TEST /I..\..\..\include /I..\..\..\external\intel_avb\lib /I..\..\..\intel-ethernet-regs\gen /I.
LDFLAGS=/nologo /DEBUG
LIBS=kernel32.lib user32.lib advapi32.lib

all: dirs $(OUTDIR)\corrected_i226_tas_test.exe

dirs:
	@if not exist $(OUTDIR) mkdir $(OUTDIR)

OBJS=$(OUTDIR)\corrected_i226_tas_test.obj

$(OUTDIR)\corrected_i226_tas_test.obj: corrected_i226_tas_test.c
	$(CC) $(CFLAGS) /Fo$(OUTDIR)\ /c corrected_i226_tas_test.c

$(OUTDIR)\corrected_i226_tas_test.exe: $(OBJS)
	$(LD) $(LDFLAGS) /OUT:$(OUTDIR)\corrected_i226_tas_test.exe $(OBJS) $(LIBS)

clean:
	@if exist "$(OUTDIR)\corrected_i226_tas_test.*" del /Q "$(OUTDIR)\corrected_i226_tas_test.*"

.PHONY: all dirs clean