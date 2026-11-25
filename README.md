# A Bit Better Traversal Movement

Source code of **A Bit Better Traversal Movement**, a mod for FINAL FANTASY VII REBIRTH (PC) that makes moving around the field feel less locked and stiff:
- faster traversal animations (vaults, climbs, jump-downs, cliff stops, climbing walls, ladders, ropes, narrow gaps);
- snappier run starts, stops and turns;
- steering while falling and right after landing;
- landing where you actually fall, and keeping some momentum on landing;
- longer dodge rolls outside combat that can roll off small ledges (big drops and out-of-bounds edges still stop you);
- rolls no longer turn into climbs mid-roll;
- control comes back at the end of a landing, vault or climb instead of after its whole animation;
- a short pause between big traversals so their animations don't cut each other off;
- the Acrobacias prompt only shows when you're actually near the ledge, and ledges a bit to the side count too;
- no running in place against walls, and a hands-on-wall pose against tall walls.

Every option lives in `TraversalSpeed.ini` and is re-read live while the game runs.

- How to build it: [BUILDING.md](BUILDING.md)
- Player instructions: [release/README.txt](release/README.txt)

This repository contains no game files. The mod is a single DLL; it patches nothing on disk.

Made with the help of an AI coding assistant (Claude by Anthropic). The author directed, tested and tuned it.

License: [MIT](LICENSE).
