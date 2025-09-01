# Diagnostic Test Tool Makefile
# Build: nmake -f avb_diagnostic.mak CFG=Debug PLATFORM=x64

!IF "$(CFG)" == ""
CFG=Debug
!ENDIF

!IF "$(PLATFORM)" == ""
PLATFORM=x64
!ENDIF

!IF "$(CFG)" == "Debug"
CFLAGS = /nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DAVB_DIAGNOSTIC_USERMODE /I.
LINKFLAGS = /nologo /DEBUG
!ELSE
CFLAGS = /nologo /W4 /O2 /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DAVB_DIAGNOSTIC_USERMODE /I.
LINKFLAGS = /nologo
!ENDIF

OUTDIR = build\tools\avb_test\$(PLATFORM)\$(CFG)
TARGET = $(OUTDIR)\avb_diagnostic_test.exe

SOURCES = tools\avb_test\avb_diagnostic_test_um.c

all: dirs $(TARGET)

dirs:
	@if not exist $(OUTDIR) mkdir $(OUTDIR)

OBJS=$(OUTDIR)\avb_diagnostic_test_um.obj

$(OUTDIR)\avb_diagnostic_test_um.obj: $(SOURCES)
	cl $(CFLAGS) /Fo$(OUTDIR)\ /c $(SOURCES)

$(TARGET): $(OBJS)
	link $(LINKFLAGS) /OUT:$(TARGET) $(OBJS) kernel32.lib user32.lib advapi32.lib

clean:
	@if exist build\tools\avb_test\$(PLATFORM)\$(CFG)\avb_diagnostic_test.* del build\tools\avb_test\$(PLATFORM)\$(CFG)\avb_diagnostic_test.*

.PHONY: all dirs clean