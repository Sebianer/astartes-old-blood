#!/bin/sh
# Builds the product hook: build/dinput8.dll (dinput8 proxy + MinHook), from hook/astartes.cpp and the generated hook/gen_data.h.
cd "$(dirname "$0")"
mkdir -p ../build/obj
MH=../third_party/minhook
cmd //c "..\tools\msvc.bat cl /nologo /O2 /MT /LD /EHsc /std:c++17 /W3 /I $MH/include /Fo..\build\obj\ astartes.cpp $MH/src/buffer.c $MH/src/hook.c $MH/src/trampoline.c $MH/src/hde/hde64.c /Fe..\build\dinput8.dll user32.lib gdi32.lib winmm.lib opengl32.lib" > ../build/hook.log 2>&1 \
  || { grep -E "error|warning C4(700|701|703|715)" ../build/hook.log; exit 1; }
grep -E "warning" ../build/hook.log | grep -v -E "minhook|C4996" | head -20
echo built ../build/dinput8.dll
