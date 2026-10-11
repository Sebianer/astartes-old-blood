#!/bin/sh
# Send commands to the lab DLL and print the new log output.
D="$LOCALAPPDATA/AstartesOldBlood"; n=$(wc -l < "$D/lab.log")
printf '%s\n' "$@" > "$D/cmd.txt.tmp"; mv "$D/cmd.txt.tmp" "$D/cmd.txt"
for i in $(seq 1 100); do [ -f "$D/cmd.txt" ] || break; sleep 0.2; done; sleep ${LABWAIT:-0.5}
tail -n +$((n+1)) "$D/lab.log"
