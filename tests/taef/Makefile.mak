# Build AvbTaefTests.dll with Microsoft C/C++. Requires WEX/TAEF include/lib path variables.
# Usage: nmake -f tests\taef\Makefile.mak [CFG=Debug|Release] [PLATFORM=x64|Win32] TAEF_INCLUDE=... TAEF_LIB=...

!IFNDEF CFG
CFG=Debug
!ENDIF

!IFNDEF PLATFORM
PLATFORM=x64
!ENDIF

!IFNDEF TAEF_INCLUDE
!ERROR Specify TAEF_INCLUDE (path to Wex headers)
!ENDIF

!IFNDEF TAEF_LIB
!ERROR Specify TAEF_LIB (path to Wex libs for selected platform)
!ENDIF

ROOT=..\..
OUTDIR=$(ROOT)\build\tests\taef\$(PLATFORM)\$(CFG)
CC=cl
LD=link
CFLAGS=/nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I"$(TAEF_INCLUDE)" /I. /I$(ROOT)
!IF "$(CFG)" == "Release"
CFLAGS=$(CFLAGS) /O2 /MD
!ELSE
CFLAGS=$(CFLAGS) /MDd
!ENDIF
LDFLAGS=/nologo /DEBUG /DLL
LIBS=kernel32.lib user32.lib advapi32.lib Wex.Common.lib Wex.Logger.lib TE.Common.lib

all: dirs $(OUTDIR)\AvbTaefTests.dll

SRC=AvbTaefTests.cpp
OBJS=$(OUTDIR)\AvbTaefTests.obj

$(OUTDIR)\AvbTaefTests.obj: $(SRC)
	$(CC) $(CFLAGS) /Fo$(OUTDIR)\ /c $(SRC)

$(OUTDIR)\AvbTaefTests.dll: $(OBJS)
	$(LD) $(LDFLAGS) /OUT:$(OUTDIR)\AvbTaefTests.dll $(OBJS) /LIBPATH:"$(TAEF_LIB)" $(LIBS)

clean:
	@if exist $(ROOT)\build\tests\taef rmdir /s /q $(ROOT)\build\tests\taef

.PHONY: all dirs clean

dirs:
	@if not exist $(OUTDIR) mkdir $(OUTDIR)
