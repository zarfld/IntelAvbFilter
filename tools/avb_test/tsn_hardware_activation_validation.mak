# nmake -f tools\avb_test\tsn_hardware_activation_validation.mak
# Build TSN Hardware Activation Validation Test
# 
# Purpose: Validates that TSN features (TAS, FP, PTM) actually activate at the 
# hardware level, not just succeed at the IOCTL level. This test addresses
# the hardware activation failures identified in comprehensive testing.

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

all: dirs $(OUTDIR)\tsn_hardware_activation_validation.exe

dirs:
	@if not exist $(OUTDIR) mkdir $(OUTDIR)

OBJS=$(OUTDIR)\tsn_hardware_activation_validation.obj

$(OUTDIR)\tsn_hardware_activation_validation.obj: tools\avb_test\tsn_hardware_activation_validation.c
	$(CC) $(CFLAGS) /Fo$(OUTDIR)\ /c tools\avb_test\tsn_hardware_activation_validation.c

$(OUTDIR)\tsn_hardware_activation_validation.exe: $(OBJS)
	$(LD) $(LDFLAGS) /OUT:$(OUTDIR)\tsn_hardware_activation_validation.exe $(OBJS) $(LIBS)

clean:
	@if exist build\tools\avb_test\$(PLATFORM)\$(CFG)\tsn_hardware_activation_validation.* del build\tools\avb_test\$(PLATFORM)\$(CFG)\tsn_hardware_activation_validation.*

.PHONY: all dirs clean