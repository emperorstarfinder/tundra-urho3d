@echo off
IF EXIST build-vs2013-x86\tundra-urho3d.sln del /Q build-vs2013-x86\tundra-urho3d.sln
cd tools\Windows\
call RunCMake "Visual Studio 12" %*
cd ..\..
pause
