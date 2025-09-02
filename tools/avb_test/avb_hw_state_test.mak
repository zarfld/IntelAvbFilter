# nmake -f tools\avb_test\avb_hw_state_test.mak
# Build AVB Hardware State Test Tool - Simple Implementation

!IFNDEF CFG
CFG=Debug
!ENDIF

!IFNDEF PLATFORM
PLATFORM=x64
!ENDIF

OUTDIR=build\tools\avb_test\$(PLATFORM)\$(CFG)
CC=cl
LD=link
CFLAGS=/nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I.
LDFLAGS=/nologo /DEBUG
LIBS=kernel32.lib user32.lib advapi32.lib

all: dirs $(OUTDIR)\avb_hw_state_test.exe

dirs:
	@if not exist $(OUTDIR) mkdir $(OUTDIR)

# Create a simple hardware state test on-the-fly (simplified version)
$(OUTDIR)\avb_hw_state_test.exe: dirs
	@echo #include ^<stdio.h^> > $(OUTDIR)\temp_hwstate.c
	@echo #include ^<windows.h^> >> $(OUTDIR)\temp_hwstate.c
	@echo int main(void) { >> $(OUTDIR)\temp_hwstate.c
	@echo     printf("Intel AVB Filter Driver - Hardware State Test Tool\\n"); >> $(OUTDIR)\temp_hwstate.c
	@echo     printf("===================================================\\n"); >> $(OUTDIR)\temp_hwstate.c
	@echo     printf("Purpose: Test hardware state management\\n\\n"); >> $(OUTDIR)\temp_hwstate.c
	@echo     HANDLE device = CreateFileW(L"\\\\.\\IntelAvbFilter", GENERIC_READ ^| GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL); >> $(OUTDIR)\temp_hwstate.c
	@echo     if (device == INVALID_HANDLE_VALUE) { >> $(OUTDIR)\temp_hwstate.c
	@echo         printf("? Failed to open device: %%lu\\n", GetLastError()); >> $(OUTDIR)\temp_hwstate.c
	@echo         return 2; >> $(OUTDIR)\temp_hwstate.c
	@echo     } >> $(OUTDIR)\temp_hwstate.c
	@echo     printf("? Device opened successfully\\n"); >> $(OUTDIR)\temp_hwstate.c
	@echo     printf("?? For detailed state testing, use avb_multi_adapter_test.exe\\n"); >> $(OUTDIR)\temp_hwstate.c
	@echo     printf("?? For comprehensive analysis, use the full test suite\\n"); >> $(OUTDIR)\temp_hwstate.c
	@echo     CloseHandle(device); >> $(OUTDIR)\temp_hwstate.c
	@echo     return 0; >> $(OUTDIR)\temp_hwstate.c
	@echo } >> $(OUTDIR)\temp_hwstate.c
	$(CC) $(CFLAGS) /Fe$(OUTDIR)\avb_hw_state_test.exe $(OUTDIR)\temp_hwstate.c $(LIBS)
	@del $(OUTDIR)\temp_hwstate.c $(OUTDIR)\temp_hwstate.obj 2>nul

clean:
	@if exist "$(OUTDIR)\avb_hw_state_test.*" del /Q "$(OUTDIR)\avb_hw_state_test.*"
	@if exist "$(OUTDIR)\temp_hwstate.*" del /Q "$(OUTDIR)\temp_hwstate.*"

.PHONY: all dirs clean