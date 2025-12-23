# I226 Advanced Test Tool Makefile
# Build: nmake -f avb_i226_advanced.mak CFG=Debug PLATFORM=x64

!IF "$(CFG)" == ""
CFG=Debug
!ENDIF

!IF "$(PLATFORM)" == ""
PLATFORM=x64
!ENDIF

!IF "$(CFG)" == "Debug"
CFLAGS = /nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DAVB_I226_ADVANCED_USERMODE /I../../../include /I../../../external/intel_avb/lib /I../../../intel-ethernet-regs/gen /I.
LINKFLAGS = /nologo /DEBUG
!ELSE
CFLAGS = /nologo /W4 /O2 /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DAVB_I226_ADVANCED_USERMODE /I../../../include /I../../../external/intel_avb/lib /I../../../intel-ethernet-regs/gen /I.
LINKFLAGS = /nologo
!ENDIF

OUTDIR = ..\..\..\build\tools\avb_test\$(PLATFORM)\$(CFG)
TARGET = $(OUTDIR)\avb_i226_advanced_test.exe

SOURCES = avb_i226_advanced_test.c

all: $(TARGET)

$(OUTDIR):
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"

$(TARGET): $(OUTDIR) $(SOURCES)
	cl $(CFLAGS) /Fo$(OUTDIR)\ /c $(SOURCES)
	link $(LINKFLAGS) /OUT:$(TARGET) $(OUTDIR)\avb_i226_advanced_test.obj kernel32.lib user32.lib advapi32.lib

clean:
	@if exist "$(OUTDIR)\*.obj" del /Q "$(OUTDIR)\*.obj"
	@if exist "$(OUTDIR)\*.pdb" del /Q "$(OUTDIR)\*.pdb"
	@if exist "$(TARGET)" del /Q "$(TARGET)"

.PHONY: all clean