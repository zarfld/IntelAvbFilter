@echo off
"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -z "C:\Users\dzarf\Documents\minidumps\022126-9421-01.dmp" -c ".logopen C:\Users\dzarf\source\repos\IntelAvbFilter\crash_report.txt; .sympath srv*https://msdl.microsoft.com/download/symbols; .reload; !analyze -v; k; kv; r; lm m Intel*; lm m *Filter*; .lastevent; .logclose; q"
echo Analysis complete!
