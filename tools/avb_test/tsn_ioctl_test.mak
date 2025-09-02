# nmake -f tools\avb_test\tsn_ioctl_test.mak
# Build TSN IOCTL Handler Verification Test

!IFNDEF CFG
CFG=Debug
!ENDIF

!IFNDEF PLATFORM
PLATFORM=x64
!ENDIF

OUTDIR=build\tools\avb_test\$(PLATFORM)\$(CFG)
CC=cl
LD=link

# Fix: Add proper x64 flags to ensure x64 compilation
CFLAGS=/nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DAVB_TEST_USERMODE /I. /DWIN64 /D_WIN64
LDFLAGS=/nologo /DEBUG /MACHINE:X64
LIBS=kernel32.lib user32.lib advapi32.lib

all: dirs $(OUTDIR)\test_tsn_ioctl_handlers.exe

dirs:
	@if not exist $(OUTDIR) mkdir $(OUTDIR)

OBJS=$(OUTDIR)\test_tsn_ioctl_handlers_um.obj

$(OUTDIR)\test_tsn_ioctl_handlers_um.obj: tools\avb_test\test_tsn_ioctl_handlers_um.c
	$(CC) $(CFLAGS) /Fo$(OUTDIR)\ /c tools\avb_test\test_tsn_ioctl_handlers_um.c

$(OUTDIR)\test_tsn_ioctl_handlers.exe: $(OBJS)
	$(LD) $(LDFLAGS) /OUT:$(OUTDIR)\test_tsn_ioctl_handlers.exe $(OBJS) $(LIBS)

clean:
	@if exist build\tools\avb_test\$(PLATFORM)\$(CFG)\test_tsn_ioctl_handlers.* del build\tools\avb_test\$(PLATFORM)\$(CFG)\test_tsn_ioctl_handlers.*

.PHONY: all dirs clean