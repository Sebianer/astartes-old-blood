#!/bin/sh
# Lab cycle: close Old Blood, install build/dinput8.dll, start it windowed and key through to the save-slot screen
# with slot 2 highlighted (DOWN once). Stops there: confirm the highlight with tools/shot.ps1 before pressing ENTER.
# Only slots 2 and 3 may be used for testing.
cd "$(dirname "$0")/.."
G="/c/Program Files (x86)/Steam/steamapps/common/Wolfenstein The Old Blood"
L="$LOCALAPPDATA/AstartesOldBlood/hook.log"
taskkill //IM WolfOldBlood_x64.exe //F >/dev/null 2>&1; sleep 3
cp "${1:-build/dinput8.dll}" "$G/dinput8.dll" || exit 1   # arg: DLL to install (lab/dinput8.dll for research)
"/c/Program Files (x86)/Steam/steam.exe" -applaunch 350080 +set r_fullscreen 0 +set r_windowWidth 1600 +set r_windowHeight 900 &
sleep 5
for i in $(seq 1 40); do grep -q "swap hooks" "$L" 2>/dev/null && break; sleep 1; done
sleep 35
powershell -NoProfile -File tools/keys.ps1 "ENTER,WAIT:6000,ENTER,WAIT:6000,DOWN,WAIT:800"
echo "at slot screen (verify slot 2 highlighted)"
