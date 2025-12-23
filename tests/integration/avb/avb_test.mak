# nmake -f avb_test.mak

!IFNDEF CFG
CFG=Debug
!ENDIF

!IFNDEF PLATFORM
PLATFORM=x64
!ENDIF

OUTDIR=..\..\..\build\tools\avb_test\$(PLATFORM)\$(CFG)
CC=cl
LD=link
CFLAGS=/nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DAVB_TEST_USERMODE /I..\..\..\include /I..\..\..\external\intel_avb\lib /I..\..\..\intel-ethernet-regs\gen /I.
LDFLAGS=/nologo /DEBUG
LIBS=kernel32.lib user32.lib advapi32.lib

all: dirs $(OUTDIR)\avb_test_i210.exe

dirs:
	@if not exist $(OUTDIR) mkdir $(OUTDIR)

OBJS=$(OUTDIR)\avb_test_um.obj

$(OUTDIR)\avb_test_um.obj: avb_test_um.c
	$(CC) $(CFLAGS) /Fo$(OUTDIR)\ /c avb_test_um.c

$(OUTDIR)\avb_test_i210.exe: $(OBJS)
	$(LD) $(LDFLAGS) /OUT:$(OUTDIR)\avb_test_i210.exe $(OBJS) $(LIBS)

clean:
	@if exist "$(OUTDIR)\avb_test_i210.exe" del /Q "$(OUTDIR)\avb_test_i210.exe"
	@if exist "$(OUTDIR)\avb_test_i210.pdb" del /Q "$(OUTDIR)\avb_test_i210.pdb"
	@if exist "$(OUTDIR)\avb_test_um.obj" del /Q "$(OUTDIR)\avb_test_um.obj"
	@if exist "$(OUTDIR)\avb_test_um.pdb" del /Q "$(OUTDIR)\avb_test_um.pdb"
