A Bit Better Traversal Movement  v1.2.0
Final Fantasy VII Rebirth (PC, game version 1.0.0.5)

Makes moving around the field feel less locked and stiff: faster traversal animations (vaults, drops, climbing walls,
ladders, ropes, narrow gaps), snappier run starts, stops and turns, steering while falling,
longer dodge rolls that can carry you off small ledges, landing where you actually fall, no running in place
against walls, a camera that follows you down when you drop off a ledge, and more. Every setting is in TraversalSpeed.ini and applies live while you play.

WHAT'S NEW IN 1.2
- Ledges a bit to the side now count: when there's no ledge straight ahead, the mod also looks up to 35 degrees to
  either side of where the character faces ([traversal] side_angle; 0 = straight ahead only, like the normal game).
- A roll can still turn into a climb or jump-down at its very start (pressing the button right next to a ledge), but
  not when it reaches a wall later ([traversal] roll_vault_start=0.15).
- Wall stop no longer stops you at party members or other characters (it missed some in busy areas).
- Optional [transitions] settings to shorten animation blends (off by default: 1 = the game's own blends).
Updating from 1.0 or 1.1: just replace dwmapi.dll. Your TraversalSpeed.ini keeps your values and gets the new keys on
the first start.

WHAT'S NEW IN 1.1
- Climbing walls, ladders, ropes/poles/zip lines/grappling hook and narrow gaps play faster ([speed] climb_wall,
  ladder, rope, squeeze). The pauses between handholds on climbing walls are much shorter.
- Snappier running: starting to run, stopping and turning around play faster ([speed] run_start, run_stop, turn).
  The running loop itself is unchanged.
- [speed] custom= lets you set your own speed for any animation family.
- Applies to every party member.
INSTALL
1. Open the game folder: Steam > Final Fantasy VII Rebirth > right-click > Manage > Browse local files.
2. Copy the "End" folder from this archive into it, merging folders. You should end up with:
      <game folder>\End\Binaries\Win64\dwmapi.dll
      <game folder>\End\Binaries\Win64\TraversalSpeed.ini
3. Start the game. TraversalSpeed.log appears next to the dll; its first lines say the mod loaded.

UNINSTALL
Delete dwmapi.dll (and TraversalSpeed.ini / TraversalSpeed.log) from End\Binaries\Win64.

SETTINGS
Edit End\Binaries\Win64\TraversalSpeed.ini while the game runs; changes apply within a second (the [camera]
options are read once at game start: restart after changing them). Every option is
explained inside the file. enabled=0 turns the whole mod off.

COMPATIBILITY
- Game version 1.0.0.5 only. On any other version the mod detects it, writes that to the log and stays inactive.
- Loads through dwmapi.dll: it conflicts with any other mod that also ships a dwmapi.dll. It works next to mods
  that use other loader files (for example hid.dll, winmm.dll, dsound.dll, xinput1_3.dll).
- Linux / Steam Deck (Proton): untested. You will likely need the launch option WINEDLLOVERRIDES="dwmapi=n,b" %command%

KNOWN LIMITS / WHERE IT MIGHT GO WRONG
- free_fall (landing where you fall): in places where a jump must land on one exact spot (dungeons, platforming,
  scripted jumps) the game's placement is kept when you'd end up on a different level, but some spots may still
  behave oddly. Set free_fall=0 if a jump misbehaves.
- Rolling off ledges: unusual or very rough edges may still stop the roll, and out-of-bounds protection relies on
  max_drop (drops deeper than that always stop you at the edge).
- Wall stop: may occasionally miss a wall, or stop you against unusual geometry.
- Hands-on-wall pose: it reuses a door-push animation, so it can clip or look off on uneven walls (wall_hand=0 turns it off).
- Faster run starts/stops/turns: if the feet look like they slide on some surface, lower run_start, run_stop or turn
  (1 = the game's normal speed).
- side_angle: running along an edge, a ledge to the side can now start a jump-down. Lower side_angle (or 0) if that
  happens where you don't want it.
- prompt_reach below 1 also means you have to be closer to a ledge for the button to start a climb or drop.
- Mostly tested with Cloud in the open world. Other party members, story segments, chocobo and vehicle sections are
  less tested.
- Keyboard steering reads the keys set in [steer] keys= (default WASD); controllers are read directly.

SOURCE AND LICENSE
MIT License (see LICENSE.txt). Source: https://github.com/xavitolavi/a-bit-better-traversal-movement

FEEDBACK AND BUGS
Please report anything odd on the Nexus page: what you were doing, where, your settings if changed, and the file
TraversalSpeed.log from End\Binaries\Win64. A short video helps a lot.
