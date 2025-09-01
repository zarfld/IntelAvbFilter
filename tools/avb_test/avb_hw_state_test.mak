# nmake -f tools\avb_test\avb_hw_state_test.mak
# Build AVB Hardware State Test Tool

!IFNDEF CFG
CFG=Debug
!ENDIF

!IFNDEF PLATFORM
PLATFORM=x64
!ENDIF

OUTDIR=build\tools\avb_test\$(PLATFORM)\$(CFG)
CC=cl
LD=link
CFLAGS=/nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DAVB_HW_STATE_USERMODE /I.
LDFLAGS=/nologo /DEBUG
LIBS=kernel32.lib user32.lib advapi32.lib

all: dirs $(OUTDIR)\avb_hw_state_test.exe

dirs:
	@if not exist $(OUTDIR) mkdir $(OUTDIR)

OBJS=$(OUTDIR)\avb_hw_state_test_um.obj

$(OUTDIR)\avb_hw_state_test_um.obj: tools\avb_test\avb_hw_state_test_um.c
	$(CC) $(CFLAGS) /Fo$(OUTDIR)\ /c tools\avb_test\avb_hw_state_test_um.c

$(OUTDIR)\avb_hw_state_test.exe: $(OBJS)
	$(LD) $(LDFLAGS) /OUT:$(OUTDIR)\avb_hw_state_test.exe $(OBJS) $(LIBS)

clean:
	@if exist build\tools\avb_test\$(PLATFORM)\$(CFG)\avb_hw_state_test.* del build\tools\avb_test\$(PLATFORM)\$(CFG)\avb_hw_state_test.*

.PHONY: all dirs clean