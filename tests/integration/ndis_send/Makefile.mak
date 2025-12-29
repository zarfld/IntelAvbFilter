# Build test_ndis_send_path.exe with Microsoft C/C++
# Usage: nmake -f tests\integration\ndis_send\Makefile.mak [CFG=Debug|Release] [PLATFORM=x64|Win32]

!IFNDEF CFG
CFG=Debug
!ENDIF

!IFNDEF PLATFORM
PLATFORM=x64
!ENDIF

ROOT=..\..\..
OUTDIR=..\..\..\build\tests\integration\ndis_send\$(PLATFORM)\$(CFG)
CC=cl
LD=link
CFLAGS=/nologo /W4 /Zi /TC /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I..\..\..
!IF "$(CFG)" == "Release"
CFLAGS=$(CFLAGS) /O2 /MD
!ELSE
CFLAGS=$(CFLAGS) /MDd
!ENDIF
LDFLAGS=/nologo /DEBUG
LIBS=kernel32.lib user32.lib advapi32.lib

all: dirs $(OUTDIR)\test_ndis_send_path.exe

SRC=test_ndis_send_path.c
OBJS=$(OUTDIR)\test_ndis_send_path.obj

$(OUTDIR)\test_ndis_send_path.obj: $(SRC)
	$(CC) $(CFLAGS) /Fo$(OUTDIR)\ /c $(SRC)

$(OUTDIR)\test_ndis_send_path.exe: $(OBJS)
	$(LD) $(LDFLAGS) /OUT:$(OUTDIR)\test_ndis_send_path.exe $(OBJS) $(LIBS)

clean:
	@if exist ..\..\..\build\tests\integration\ndis_send rmdir /s /q ..\..\..\build\tests\integration\ndis_send

.PHONY: all dirs clean

dirs:
	@if not exist $(OUTDIR) mkdir $(OUTDIR)
