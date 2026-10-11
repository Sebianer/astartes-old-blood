# Astartes: The Old Blood — mod log

## Games
- Host: Wolfenstein: The Old Blood (Steam 350080), `C:\Program Files (x86)\Steam\steamapps\common\Wolfenstein The Old Blood`,
  WolfOldBlood_x64.exe, id Tech 5, OpenGL, SteamStub-wrapped (.bind): .text is encrypted on disk, so analyse a memory dump.
- Companion: Warhammer 40,000: Space Marine 2 (Steam 2183900), `D:\SteamLibrary\steamapps\common\Space Marine 2`, Swarm engine.

## Route
- Old Blood has no loader. dinput8.dll proxy (exe imports DINPUT8 DirectInput8Create) in the game folder.
- Game state via memory: static idCVar objects + entity list (below). Overlay/HUD via SwapBuffers hook (todo).
- SM2 content read at setup from the player's paks (zip archives):
  - voice: default_sound_english.pak > sounds/desktop/external_sources_vorbis_english.zip > vo_chatter_<class>_<event>_NN.wem (Wwise vorbis)
  - class card art: default_pct_N.pak textures/ui/class_icons/cards/class_<id>.asset/class_<id>_0.pct_mip = raw RGBA8 396x630
    (header in paks/client/resources.pak *.pct.resource: format 0, sx/sy)
- SM2 class ids: tank=Bulwark, support=Heavy, raider=Assault, berserk(card)/berserker(voice)=Vanguard, soldier=Tactical,
  sniper=Sniper, engineer=Techmarine.

## Old Blood internals (build with image size 0x247a000, verified 2026-10-11)
- static idCVar (0x80 bytes) S: +0x08 value str ptr (-> inline +0x10 buf[16]), +0x28 int, +0x2c float, +0x30 name,
  +0x38 default str, +0x40 desc, +0x48 flags, +0x70 next. Found by scanning image for objects whose +0x08 == S+0x10.
  Writing int/float/str directly works (no separate internal var); retail resets "cheat" cvars at init only.
- Useful cvars: pm_runspeed 260, pm_sprintspeed 450, pm_jumpheight 62, g_damageScale (damage on player), ai_damageScale,
  g_permaGodMode, timescale, g_playerHealthRegen{Speed,Delay,Steps}, com_skipIntroVideo (flags 0x20000: +set ignored).
- Command line `+set r_fullscreen 0 +set r_windowWidth 1600 +set r_windowHeight 900` works for lab windowing.
- RTTI vtables (rva): idPlayer 0x1004c38, idAI2 0xf0d088, idGameLocal 0xfdf1c8, idAIHealth 0xf0ccc8, idActor 0xf41568.
- gameLocal* at image+0x1823f68; entities[4096] at gameLocal+0x161D0; entities[0]=player; entity name char* at +0x20.
- entity vfunc +0x300 -> health component; component vfunc +0x78 = health (float), +0x80 = max health.

- AI health component (idAIHealth vt 0xf0ccc8): health float at +0x10 (getter img+0x52f4e0 = movss xmm0,[rcx+10h]),
  max at +0x0c. Writing it reads back through the getter. Player comp: health +0x08, armour +0x10/+0x14 (as before).
- Zero AI health does NOT kill; posting `kill` (img+0x81b020(ent, evKill, 0)) from our thread doesn't crash but doesn't
  kill either; posting an event with a float arg (decreaseHealth) throws inside the call. So: damage multipliers are done
  by dividing AI current health (deaths stay in the game's own code); the turret leaves its target at 0.5 hp.
- gameLocal+0x46208/+0x46210 "active list" holds every AI of the level (placed + dormant), not only awake ones.
- Rendering: OpenGL 3.2 core profile (mask 1). HUD draws in its own compat context made with wglCreateContext on the
  game's HDC inside the SwapBuffers hook, then switches back - works, no state save needed.
- cvars verified live: pm_runspeed/sprintspeed/jumpheight, g_damageScale (0 = no damage), timescale hold when written;
  com_skipIntroVideo held at 1 from DLL load skips the intro videos. None of them are written to wolftobConfig.cfg.
- Level start shows "Press [E] to continue" with the player entity already present and still: deploy = first move/hurt.

## Gotchas
1. Never write C++ with backslash escapes through shell heredocs: "\\dinput8.dll" lost a backslash, LoadLibrary failed, game
   crashed 1s after start. Write sources with the editor tool; test the proxy with lab/harness.exe before installing.
2. Fullscreen Old Blood minimises when another window takes focus; lab runs use the windowed +set args, never edit the cfg.
4. Testing: ONLY save slot 2 or 3 (user's real progress is slot 1). tools/restart.sh stops on the slot screen with DOWN
   pressed once - screenshot (tools/shot.ps1) and confirm slot 2 before ENTER. Loading a slot writes a checkpoint.
7. Melty test runs: a launch exe that exits immediately made Melty skip starting run 2 ("game didn't appear"). Keep the
   launcher alive until the game exits.
5. Python/sed edits of C++ through heredocs eat "\n" escapes (same as gotcha 1) - use the Edit tool for C++ strings.
6. MinHook's hook.c and a hook.cpp both make hook.obj; the product source is hook/astartes.cpp for that reason.
3. Byte-wise memory scans of the whole process take minutes; findptr scans heap before the image (cap hits first).

## Restore
- Remove `dinput8.dll` from the Old Blood folder. Saves backup: ~/.universal-modder/backups/tob-saves/.
