Astartes: The Old Blood
=======================
Play Wolfenstein: The Old Blood as a Space Marine 2 class: Heavy, Assault, Techmarine, Tactical, Vanguard,
Bulwark or Sniper. Each class has its own movement, toughness and damage, an ability on V, and its own voice lines.

How it works
- AstartesLauncher.exe shows the class picker. On first start it reads the class card art, voice lines and ability
  sounds from YOUR OWN Space Marine 2 install into %LOCALAPPDATA%\AstartesOldBlood\content. No Space Marine 2 files
  are shipped with this mod.
- dinput8.dll (in the Old Blood folder) plays the class you picked. Without a pick, or on an unknown game build, it
  does nothing.
- To remove: delete dinput8.dll and the AstartesOldBlood folder from the Old Blood folder, and
  %LOCALAPPDATA%\AstartesOldBlood.

Licence
- Astartes: The Old Blood (launcher, dinput8.dll hook, data sheets) is free software under the GNU General Public
  License v3.0: LICENSE.txt. It covers only this mod's own code, not any Wolfenstein or Space Marine 2 content.

Credits
- Wolfenstein: The Old Blood (c) ZeniMax Media / MachineGames. Warhammer 40,000: Space Marine 2 (c) Games Workshop /
  Saber Interactive / Focus Entertainment. Not affiliated with or endorsed by any of them.
- vgmstream (https://github.com/vgmstream/vgmstream), release r2117, decodes the Space Marine 2 audio. Licence:
  vgmstream/COPYING.
- MinHook by Tsuda Kageyu (https://github.com/TsudaKageyu/minhook), compiled into dinput8.dll. Licence:
  LICENSE-minhook.txt.
