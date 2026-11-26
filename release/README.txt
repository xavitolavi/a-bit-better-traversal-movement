A Bit Better Traversal Movement  v1.3.1
Final Fantasy VII Rebirth (PC, game version 1.0.0.5 or 1.0.0.4)

Makes moving around the field feel less locked and stiff: faster traversal animations (vaults, drops, climbing walls,
ladders, ropes, narrow gaps), snappier run starts, stops and turns, steering while falling,
longer dodge rolls that can carry you off small ledges, landing where you actually fall, no running in place
against walls, a camera that follows you down when you drop off a ledge, and more. Every setting is in TraversalSpeed.ini and applies live while you play.

WHAT'S NEW IN 1.3.1
- Rolling off ledges reaches more places: drops deeper than max_drop are allowed too when the ground below is part
  of the game's own walkable area (the map the party and enemies walk on), up to 25 m ([roll] max_drop_walkable).
  Lower floors, broken floors and the levels below platforms you'd reach anyway now work; out-of-bounds ground isn't
  on that map, so those edges still stop you. max_drop_walkable=0 = the 1.3 behaviour.
- Drops need you closer to the edge: the Acrobacias prompt for jump-downs shows at 60% of the normal prompt reach
  ([traversal] drop_prompt_reach), so you don't get the drop prompt while just walking near an edge. Climbs and
  vaults keep their distance.
- Fixed: getting stuck against a wall (unable to move until letting go of the stick) - the wall stop no longer
  holds you when the game itself isn't pushing you into something, and loose debris / movable props don't count
  as walls.
- Game version 1.0.0.4 support (beta): one dwmapi.dll for 1.0.0.5 and 1.0.0.4, it recognises the version at startup
  (TraversalSpeed.log says "game build 1.0.0.4" or "game build 1.0.0.5"). Not yet played on 1.0.0.4: if you play
  that version, please report how it goes and send TraversalSpeed.log.
Updating: just replace dwmapi.dll. Your TraversalSpeed.ini keeps your values and gets the new keys on the first start.

WHAT'S NEW IN 1.3
- Control comes back at the end of a traversal instead of after its whole animation ([transitions] landing_cancel):
  pressing dodge / Acrobacias while landing from a drop rolls or starts the next traversal right away, and at the end
  of a vault or climb (feet already on the floor) the button or the stick ends it at once.
- The short pause after a traversal only holds back automatic traversals; the button always answers right away.
- Ledges found to the side never start an automatic vault or drop (no accidental drops running along an edge);
  they still offer the prompt.
- Pushing diagonally into a wall slides along it instead of stopping.
- Rolls from standing still go as far as running ones ([roll] distance_standing), and rolls keep their reach up
  slopes ([roll] max_rise).
Updating: just replace dwmapi.dll. Your TraversalSpeed.ini keeps your values and gets the new keys on the first start.

WHAT'S NEW IN 1.2
- Ledges a bit to the side now count: when there's no ledge straight ahead, the mod also looks up to 35 degrees to
  either side of where the character faces ([traversal] side_angle; 0 = straight ahead only, like the normal game).
- A roll can still turn into a climb or jump-down at its very start (pressing the button right next to a ledge), but
  not when it reaches a wall later ([traversal] roll_vault_start=0.15).
- Wall stop no longer stops you at party members or other characters (it missed some in busy areas).
- Optional [transitions] settings to shorten animation blends (off by default: 1 = the game's own blends).
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
- Game versions 1.0.0.5 and 1.0.0.4 (1.0.0.4 in beta). On any other version the mod detects it, writes that to the log
  and stays inactive.
- Loads through dwmapi.dll: it conflicts with any other mod that also ships a dwmapi.dll. It works next to mods
  that use other loader files (for example hid.dll, winmm.dll, dsound.dll, xinput1_3.dll).
- Linux / Steam Deck (Proton): untested. You will likely need the launch option WINEDLLOVERRIDES="dwmapi=n,b" %command%

KNOWN LIMITS / WHERE IT MIGHT GO WRONG
- free_fall (landing where you fall): in places where a jump must land on one exact spot (dungeons, platforming,
  scripted jumps) the game's placement is kept when you'd end up on a different level, but some spots may still
  behave oddly. Set free_fall=0 if a jump misbehaves.
- Rolling off ledges: unusual or very rough edges may still stop the roll. Out-of-bounds protection: drops deeper
  than max_drop only go when the ground below is on the game's walkable area (and at most max_drop_walkable).
- Wall stop: may occasionally miss a wall, or stop you against unusual geometry.
- Hands-on-wall pose: it reuses a door-push animation, so it can clip or look off on uneven walls (wall_hand=0 turns it off).
- Faster run starts/stops/turns: if the feet look like they slide on some surface, lower run_start, run_stop or turn
  (1 = the game's normal speed).
- landing_cancel: it ends traversals the way the game itself does, but always on the ground; if a scripted jump or
  platforming section misbehaves, set landing_cancel=0 and please tell me where.
- side_angle: ledges to the side only offer the prompt (since 1.3 they never start an automatic traversal).
- prompt_reach below 1 also means you have to be closer to a ledge for the button to start a climb or drop.
- Mostly tested with Cloud in the open world. Other party members, story segments, chocobo and vehicle sections are
  less tested.
- Keyboard steering reads the keys set in [steer] keys= (default WASD); controllers are read directly.

SOURCE AND LICENSE
MIT License (see LICENSE.txt). Source: https://github.com/xavitolavi/a-bit-better-traversal-movement

FEEDBACK AND BUGS
Please report anything odd on the Nexus page: what you were doing, where, your settings if changed, and the file
TraversalSpeed.log from End\Binaries\Win64. A short video helps a lot.
