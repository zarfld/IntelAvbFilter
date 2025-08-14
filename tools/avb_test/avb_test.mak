# nmake -f tools\avb_test\avb_test.mak

!IFNDEF CFG
CFG=Debug
!ENDIF

!IFNDEF PLATFORM
PLATFORM=x64
!ENDIF

OUTDIR=build\tools\avb_test\$(PLATFORM)\$(CFG)
CC=cl
LD=link
CFLAGS=/nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DAVB_TEST_USERMODE
LDFLAGS=/nologo /DEBUG
LIBS=kernel32.lib user32.lib advapi32.lib

all: dirs $(OUTDIR)\avb_test_i210.exe

dirs:
	@if not exist $(OUTDIR) mkdir $(OUTDIR)

OBJS=$(OUTDIR)\avb_test_actual.obj

$(OUTDIR)\avb_test_actual.obj: tools\avb_test\avb_test_actual.c
	$(CC) $(CFLAGS) /Fo$(OUTDIR)\ /c tools\avb_test\avb_test_actual.c

$(OUTDIR)\avb_test_i210.exe: $(OBJS)
	$(LD) $(LDFLAGS) /OUT:$(OUTDIR)\avb_test_i210.exe $(OBJS) $(LIBS)

clean:
	@if exist build\tools\avb_test rmdir /s /q build\tools\avb_test
