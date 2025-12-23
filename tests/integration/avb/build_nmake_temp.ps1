& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location 'C:\Users\dzarf\source\repos\IntelAvbFilter\tests\integration\avb'
nmake /f 'avb_test.mak'
exit $LASTEXITCODE
