#!/bin/sh
# Builds everything and writes build/AstartesOldBlood-<version>.zip:
#   dinput8.dll                      -> Old Blood folder
#   AstartesOldBlood/                -> Old Blood folder (launcher, vgmstream, credits)
cd "$(dirname "$0")/.."
VER=${1:-1.0.1}
python -I tools/gen.py || exit 1
sh hook/build.sh || exit 1
mkdir -p build/AstartesOldBlood
/c/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe -nologo -target:winexe -optimize '-out:build\AstartesOldBlood\AstartesLauncher.exe' \
  -r:System.Windows.Forms.dll -r:System.Drawing.dll -r:System.Core.dll 'launcher\*.cs' || exit 1
P=build/package; rm -rf $P; mkdir -p $P/AstartesOldBlood/vgmstream
cp build/dinput8.dll $P/
cp build/AstartesOldBlood/AstartesLauncher.exe tools/README.txt $P/AstartesOldBlood/
cp third_party/minhook/LICENSE.txt $P/AstartesOldBlood/LICENSE-minhook.txt
cp LICENSE $P/AstartesOldBlood/LICENSE.txt
cp third_party/vgmstream/*.dll third_party/vgmstream/vgmstream-cli.exe third_party/vgmstream/COPYING $P/AstartesOldBlood/vgmstream/
rm -f build/AstartesOldBlood-$VER.zip
# zip with forward-slash paths (Windows PowerShell 5.1's Compress-Archive can write backslashes)
python -I -c "
import os, sys, zipfile
src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED) as z:
    for d, _, fs in os.walk(src):
        for f in sorted(fs):
            p = os.path.join(d, f); z.write(p, os.path.relpath(p, src).replace(os.sep, '/'))
" $P build/AstartesOldBlood-$VER.zip
ls -la build/AstartesOldBlood-$VER.zip
