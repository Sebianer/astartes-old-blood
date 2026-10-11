#!/bin/sh
# Drives one Melty test run of Old Blood. Only save slot 2 may be loaded (slot 1 is the user's progress).
#   testrun.sh count           - print how many games the launcher has started so far (take it BEFORE a test)
#   testrun.sh slots <N> <png> - wait until the launcher has started game number N+1.., key through warning + title,
#                                DOWN to slot 2, screenshot to <png>. Confirm slot 2 on the screenshot!
#   testrun.sh play            - load the highlighted slot, Resume, E, move; print the hook's test checks
cd "$(dirname "$0")/.."
D="$LOCALAPPDATA/AstartesOldBlood"; L="$D/hook.log"; LL="$D/launcher.log"
runs() { grep -c "game running" "$LL" 2>/dev/null || echo 0; }
case "$1" in
count) runs ;;
slots)
  for i in $(seq 1 280); do [ "$(runs)" -gt "$2" ] && break; sleep 2; done
  [ "$(runs)" -gt "$2" ] || { echo "no new game"; exit 1; }
  for i in $(seq 1 40); do grep -q "swap hooks" "$L" 2>/dev/null && break; sleep 1; done
  head -3 "$L"
  sleep 35
  powershell -NoProfile -File tools/keys.ps1 "ENTER,WAIT:6000,ENTER,WAIT:6000,DOWN,WAIT:800"
  powershell -NoProfile -File tools/shot.ps1 "$3" ;;
play)
  powershell -NoProfile -File tools/keys.ps1 "ENTER,WAIT:5000,ENTER"
  for i in $(seq 1 60); do grep -q "player " "$L" && break; sleep 1; done
  sleep 14
  powershell -NoProfile -File tools/keys.ps1 "E,WAIT:2500,S:700,WAIT:7000"
  grep "test check" "$L" ;;
esac
