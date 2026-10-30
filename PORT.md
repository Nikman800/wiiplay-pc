# Wii Play PC port (work in progress, private)

This repository is the PC port of the Wii Play decompilation. It starts from the public matching
decompilation (https://github.com/Nikman800/wiiplay, remote `upstream`) and adds what a native build
needs. Decompilation fixes go to the decomp first and are merged here with `git pull upstream main`;
port changes stay here.

Plan (docs/PLAN.md, Phases 6-10):
- Phase 6, assets and file formats: `port/assets/` (inventory and converters for the player's own
  disc data; no Nintendo files are ever committed).
- Phase 7, platform layer: replacements for the Wii hardware and SDK (graphics, OS, disc, input,
  audio, saves).
- Phase 8, bring-up: boot to the menus, then each minigame.

Nothing here builds a playable game yet.
