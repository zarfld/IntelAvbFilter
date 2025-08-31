# Multi-Adapter Test Tool Makefile
# Build: nmake -f avb_multi_adapter.mak CFG=Debug PLATFORM=x64

!IF "$(CFG)" == ""
CFG=Debug
!ENDIF

!IF "$(PLATFORM)" == ""
PLATFORM=x64
!ENDIF

!IF "$(CFG)" == "Debug"
CFLAGS = /nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I../../
LINKFLAGS = /nologo /DEBUG
!ELSE
CFLAGS = /nologo /W4 /O2 /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I../../
LINKFLAGS = /nologo
!ENDIF

OUTDIR = ..\..\build\tools\avb_test\$(PLATFORM)\$(CFG)
TARGET = $(OUTDIR)\avb_multi_adapter_test.exe

SOURCES = avb_multi_adapter_test.c

all: $(TARGET)

$(OUTDIR):
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"

$(TARGET): $(OUTDIR) $(SOURCES)
	cl $(CFLAGS) /Fo$(OUTDIR)\ /c $(SOURCES)
	link $(LINKFLAGS) /OUT:$(TARGET) $(OUTDIR)\avb_multi_adapter_test.obj kernel32.lib user32.lib advapi32.lib

clean:
	@if exist "$(OUTDIR)\*.obj" del /Q "$(OUTDIR)\*.obj"
	@if exist "$(OUTDIR)\*.pdb" del /Q "$(OUTDIR)\*.pdb"
	@if exist "$(TARGET)" del /Q "$(TARGET)"

.PHONY: all clean