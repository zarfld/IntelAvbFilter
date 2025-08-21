# Build AvbIntegrationTests.exe with Microsoft C/C++
# Usage: nmake -f tests\integration\Makefile.mak [CFG=Debug|Release] [PLATFORM=x64|Win32]

!IFNDEF CFG
CFG=Debug
!ENDIF

!IFNDEF PLATFORM
PLATFORM=x64
!ENDIF

OUTDIR=build\tests\integration\$(PLATFORM)\$(CFG)
CC=cl
LD=link
CFLAGS=/nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I.
!IF "$(CFG)" == "Release"
CFLAGS=$(CFLAGS) /O2 /MD
!ELSE
CFLAGS=$(CFLAGS) /MDd
!ENDIF
LDFLAGS=/nologo /DEBUG
LIBS=kernel32.lib user32.lib advapi32.lib

all: dirs $(OUTDIR)\AvbIntegrationTests.exe

SRC=tests\integration\AvbIntegrationTests.cpp
OBJS=$(OUTDIR)\AvbIntegrationTests.obj

$(OUTDIR)\AvbIntegrationTests.obj: $(SRC)
	$(CC) $(CFLAGS) /Fo$(OUTDIR)\ /c $(SRC)

$(OUTDIR)\AvbIntegrationTests.exe: $(OBJS)
	$(LD) $(LDFLAGS) /OUT:$(OUTDIR)\AvbIntegrationTests.exe $(OBJS) $(LIBS)

clean:
	@if exist build\tests\integration rmdir /s /q build\tests\integration

.PHONY: all dirs clean

dirs:
	@if not exist $(OUTDIR) mkdir $(OUTDIR)
