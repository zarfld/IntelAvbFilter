# nmake -f avb_diagnostic.mak
# Build AVB Diagnostic Test Tool - Simple Implementation

!IFNDEF CFG
CFG=Debug
!ENDIF

!IFNDEF PLATFORM
PLATFORM=x64
!ENDIF

OUTDIR=..\..\build\tools\avb_test\$(PLATFORM)\$(CFG)
CC=cl
LD=link
CFLAGS=/nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I..\..\include /I..\..\external\intel_avb\lib /I..\..\intel-ethernet-regs\gen /I.
LDFLAGS=/nologo /DEBUG
LIBS=kernel32.lib user32.lib advapi32.lib

all: dirs $(OUTDIR)\avb_diagnostic_test.exe

dirs:
	@if not exist $(OUTDIR) mkdir $(OUTDIR)

# Create a simple diagnostic test on-the-fly
$(OUTDIR)\avb_diagnostic_test.exe: dirs
	@echo #include ^<stdio.h^> > $(OUTDIR)\temp_diagnostic.c
	@echo #include ^<windows.h^> >> $(OUTDIR)\temp_diagnostic.c
	@echo int main(void) { >> $(OUTDIR)\temp_diagnostic.c
	@echo     printf("Intel AVB Filter Driver - Diagnostic Test Tool\\n"); >> $(OUTDIR)\temp_diagnostic.c
	@echo     printf("==============================================\\n"); >> $(OUTDIR)\temp_diagnostic.c
	@echo     printf("Purpose: Hardware diagnostics and troubleshooting\\n\\n"); >> $(OUTDIR)\temp_diagnostic.c
	@echo     HANDLE device = CreateFileW(L"\\\\.\\IntelAvbFilter", GENERIC_READ ^| GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL); >> $(OUTDIR)\temp_diagnostic.c
	@echo     if (device == INVALID_HANDLE_VALUE) { >> $(OUTDIR)\temp_diagnostic.c
	@echo         printf("? Failed to open device: %%lu\\n", GetLastError()); >> $(OUTDIR)\temp_diagnostic.c
	@echo         printf("   Make sure Intel AVB Filter driver is installed and Intel hardware present\\n"); >> $(OUTDIR)\temp_diagnostic.c
	@echo         return 2; >> $(OUTDIR)\temp_diagnostic.c
	@echo     } >> $(OUTDIR)\temp_diagnostic.c
	@echo     printf("? Device opened successfully\\n"); >> $(OUTDIR)\temp_diagnostic.c
	@echo     printf("?? For comprehensive diagnostics, use avb_multi_adapter_test.exe\\n"); >> $(OUTDIR)\temp_diagnostic.c
	@echo     CloseHandle(device); >> $(OUTDIR)\temp_diagnostic.c
	@echo     return 0; >> $(OUTDIR)\temp_diagnostic.c
	@echo } >> $(OUTDIR)\temp_diagnostic.c
	$(CC) $(CFLAGS) /Fe$(OUTDIR)\avb_diagnostic_test.exe $(OUTDIR)\temp_diagnostic.c $(LIBS)
	@del $(OUTDIR)\temp_diagnostic.c $(OUTDIR)\temp_diagnostic.obj 2>nul

clean:
	@if exist "$(OUTDIR)\avb_diagnostic_test.*" del /Q "$(OUTDIR)\avb_diagnostic_test.*"
	@if exist "$(OUTDIR)\temp_diagnostic.*" del /Q "$(OUTDIR)\temp_diagnostic.*"

.PHONY: all dirs clean