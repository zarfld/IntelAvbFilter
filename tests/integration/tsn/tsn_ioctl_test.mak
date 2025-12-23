# nmake -f tsn_ioctl_test.mak
# Build TSN IOCTL Handler Verification Test

!IFNDEF CFG
CFG=Debug
!ENDIF

!IFNDEF PLATFORM
PLATFORM=x64
!ENDIF

OUTDIR=..\..\..\build\tools\avb_test\$(PLATFORM)\$(CFG)
CC=cl
LD=link

# Include paths: 3 levels up to repo root
CFLAGS=/nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DAVB_TEST_USERMODE /I..\..\..\include /I..\..\..\external\intel_avb\lib /I..\..\..\intel-ethernet-regs\gen /I. /DWIN64 /D_WIN64
LDFLAGS=/nologo /DEBUG /MACHINE:X64
LIBS=kernel32.lib user32.lib advapi32.lib

all: dirs $(OUTDIR)\test_tsn_ioctl_handlers.exe

dirs:
	@if not exist $(OUTDIR) mkdir $(OUTDIR)

OBJS=$(OUTDIR)\test_tsn_ioctl_handlers_um.obj

$(OUTDIR)\test_tsn_ioctl_handlers_um.obj: test_tsn_ioctl_handlers_um.c
	$(CC) $(CFLAGS) /Fo$(OUTDIR)\ /c test_tsn_ioctl_handlers_um.c

$(OUTDIR)\test_tsn_ioctl_handlers.exe: $(OBJS)
	$(LD) $(LDFLAGS) /OUT:$(OUTDIR)\test_tsn_ioctl_handlers.exe $(OBJS) $(LIBS)

clean:
	@if exist "$(OUTDIR)\test_tsn_ioctl_handlers.*" del /Q "$(OUTDIR)\test_tsn_ioctl_handlers.*"

.PHONY: all dirs clean