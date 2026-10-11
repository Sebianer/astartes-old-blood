# Astartes: The Old Blood

Play **Wolfenstein: The Old Blood** as a **Warhammer 40,000: Space Marine 2** class. Get it on Melty:
https://melty.gg/m/astartes-the-old-blood

- Class picker with the Space Marine 2 class cards and a voice line on hover.
- Per-class speed, jump, toughness and damage; an ability on **V** (Iron Halo, Jump Pack Surge, Servo Turret, Auspex
  Scan, Grapnel Rush, Banner, Camo Cloak).
- Space Marine 2 voice barks, a class HUD and a deployment moment at each level start.

Art and audio are read from the player's own Space Marine 2 install at first start; this repository and the release
contain no game files.

## Layout

| Path | What |
|---|---|
| `sheets/*.json` | All tunable data: classes, abilities, voice lines, sound effects, HUD, Old Blood memory offsets |
| `tools/gen.py` | Checks the sheets and generates `hook/gen_data.h` and `launcher/GenData.cs` |
| `hook/astartes.cpp` | `dinput8.dll` proxy loaded by Old Blood: class stats, abilities, voice, HUD (own GL context) |
| `launcher/` | C# class picker; extracts cards/voices from Space Marine 2 paks, then starts the game via Steam |
| `lab/` | Research DLL used to map Old Blood's memory (not shipped) |
| `MODLOG.md` | Verified Old Blood internals and gotchas |

## Build (Windows)

Needs Python 3, MSVC 2019 Build Tools (x64, see `tools/msvc.bat`), .NET Framework 4.x `csc`, and
[vgmstream](https://github.com/vgmstream/vgmstream) r2117 `vgmstream-win64.zip` unpacked into
`third_party/vgmstream/`.

```sh
python tools/gen.py "<Space Marine 2 folder>"   # optional arg: re-resolves sound effect ids from your paks
sh tools/package.sh 1.0.3                       # -> build/AstartesOldBlood-1.0.3.zip
```

Install: unpack the zip into the Old Blood folder and run `AstartesOldBlood/AstartesLauncher.exe --sm2 "<Space Marine 2 folder>"`.

## Licence

GPL-3.0 (see `LICENSE`). Uses [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause, in `third_party/`) and
vgmstream (ISC-style licence). Wolfenstein and Warhammer 40,000 belong to their owners; this is an unofficial fan mod.
