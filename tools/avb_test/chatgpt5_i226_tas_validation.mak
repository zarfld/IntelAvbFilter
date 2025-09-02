# nmake -f tools\avb_test\chatgpt5_i226_tas_validation.mak
# Build ChatGPT5 I226 TAS Validation Tool

!IFNDEF CFG
CFG=Debug
!ENDIF

!IFNDEF PLATFORM
PLATFORM=x64
!ENDIF

OUTDIR=..\..\build\tools\avb_test\$(PLATFORM)\$(CFG)
CC=cl
LD=link
CFLAGS=/nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DCHATGPT5_VALIDATION /I. /I..\..\external\intel_avb\include
LDFLAGS=/nologo /DEBUG
LIBS=kernel32.lib user32.lib advapi32.lib

all: dirs $(OUTDIR)\chatgpt5_i226_tas_validation.exe

dirs:
	@if not exist $(OUTDIR) mkdir $(OUTDIR)

OBJS=$(OUTDIR)\chatgpt5_i226_tas_validation.obj

$(OUTDIR)\chatgpt5_i226_tas_validation.obj: chatgpt5_i226_tas_validation.c
	$(CC) $(CFLAGS) /Fo$(OUTDIR)\ /c chatgpt5_i226_tas_validation.c

$(OUTDIR)\chatgpt5_i226_tas_validation.exe: $(OBJS)
	$(LD) $(LDFLAGS) /OUT:$(OUTDIR)\chatgpt5_i226_tas_validation.exe $(OBJS) $(LIBS)

clean:
	@if exist "$(OUTDIR)\chatgpt5_i226_tas_validation.*" del /Q "$(OUTDIR)\chatgpt5_i226_tas_validation.*"

.PHONY: all dirs clean