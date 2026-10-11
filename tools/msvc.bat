@echo off
rem Runs a command inside the MSVC x64 environment.
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
%*
