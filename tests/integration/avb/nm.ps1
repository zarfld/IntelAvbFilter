& 'C:\PROGRA~1\MICROS~1\2022\COMMUN~1\Common7\Tools\LAUNCH~1.PS1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location 'C:\Users\dzarf\source\repos\IntelAvbFilter\tests\integration\avb'
nmake /f 'avb_test.mak'
exit $LASTEXITCODE
