#!/bin/sh
# Builds the lab loader (dinput8.dll) and the hot-reloadable core into %LOCALAPPDATA%\AstartesOldBlood.
cd "$(dirname "$0")"
cmd //c "..\tools\msvc.bat cl /nologo /O2 /LD /EHa labcore.cpp /Fe:labcore.dll user32.lib" > build.log 2>&1 || { grep -i error build.log; exit 1; }
cp labcore.dll "$LOCALAPPDATA/AstartesOldBlood/labcore.dll"
if [ "$1" = "loader" ]; then cmd //c "..\tools\msvc.bat cl /nologo /O2 /LD /EHsc loader.cpp /Fe:dinput8.dll user32.lib" > build2.log 2>&1 || { grep -i error build2.log; exit 1; }; fi
echo built
