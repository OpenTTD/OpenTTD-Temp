cd ..
editbin /nologo /subsystem:console bin\openttd.exe
cd bin
cscript /nologo ai\regression\run.vbs
set RESULT=%ERRORLEVEL%
cd ..
if /i "%RESULT%" EQU "0" (
	cscript /nologo tests\regression\run.vbs
	set RESULT=%ERRORLEVEL%
)
editbin /nologo /subsystem:windows bin\openttd.exe
exit %RESULT%
