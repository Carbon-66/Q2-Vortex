# Vortex for Q2

Breakout down a tube, for the Shanling Q2 under [Q2 Pod](https://github.com/DiamondBond/q2-pod),
as a full-screen **card game** in Library → Games. The bricks sit on the inside of a tube in five
turning rings; the ball runs round the wall and down to the far end and back; your bat runs round
the near rim on the wheel.

The game is new code. Its art, level layouts, words and sound effects are read at run time from
the player's own copy of the 2006 click-wheel iPod game *Vortex* (the `.ipg` file), put in the
card folder. **No file from that game is in this repository or in the card folder as built**:
don't commit one, and don't attach one to a release.

The player's guide (install, controls, how to play) is [card/README.txt](card/README.txt), copied
into the card folder as `README.txt`.

## Building

```sh
./build.sh                        # build/q2-vortex and dist/.vortex (copy .vortex to the card's root)
VX_IPG=/path/to/your.ipg test/run.sh        # host tests: the real main() on a fake Q2 panel
VX_IPG=/path/to/your.ipg test/run.sh clear  # and level 1 played through on the autopilot
```

A release is a zip of the card folder, `.vortex/` holding `q2-vortex`, `q2app`, the icons and
`README.txt`: Q2 Pod's App Store takes the app's folder from the repository's name less `q2-`.

Needs `clang`, `ld.lld` and `python3` (with Pillow for `tools/icon.py` and the tests). Built as
q2-doom, Q2A Miami and Challenger Deep are: no MIPS sysroot. The binary matches the ABI of Q2 Pod's
own helpers: MIPS32r2 little-endian, o32, FR=1, NaN2008, glibc 2.28, interpreter
`/lib/ld-linux-mipsn8.so.1`. `sysroot/include` declares the libc used and `sysroot/stubs/gen.py`
makes stub `libc.so.6`, `libpthread.so.0` and `libasound.so.2` to link against, so the Q2's own
libraries are what run. If you add a libc call, add it to `gen.py` too. `rt/` has the entry point
crt1.o would give and libgcc's 64-bit division.

## Layout

| Path | What it is |
| --- | --- |
| `src/q2.c` | The platform: the portrait panel, Q2 Pod's input datagrams, the log, `main()` (Challenger Deep's) |
| `src/sound.c` | The mixer, on ALSA as Q2 Pod's q2video plays (`audio=1`), for the iPod game's 16-bit sounds |
| `src/zip.c` | The `.ipg` (a zip) and a raw-deflate decoder after zlib's puff |
| `src/assets.c` | The iPod game's pictures, texture bank, sound bank, levels and words |
| `src/gfx.c` | Drawing: sprites scaled, turned and lit; soft shapes; the vector font; the view's polar table |
| `src/tube.c` | The tube: the background, the door, the bricks' faces, the ball, the bat, effects, the HUD |
| `src/game.c` | The rules, bonuses, menus, help, saves and the loop |
| `card/` | `q2app`, icons and the player's README |
| `test/` | Host build, fake framebuffer (`fbshim.c`) and ALSA (`alsa_fake.c`), scripted play (`drive.py`), frame viewer |

## The iPod game's files

Worked out for this port; the `.ipg`'s executable is FairPlay-encrypted and isn't read.

- **Pictures** (`Backgrounds/*.ipd`, `UI3/*.ipd`): a 16-byte header (width, height, format, 0, as
  little-endian words), then the pixels. Formats: `0x1001` RGB565, `0x1003` RGBA4444, `0x2005`
  RGBA8888, `0x0800` and `0x1000` 8-bit alpha, `0x0809` a 256-entry RGBA palette then 8-bit indices.
  Each of the 15 themes has a 320 × 240 tube and a `_Door1` sheet of 16 frames of the far end.
- **Textures** (`tex`): the count, the largest size, each entry's size, then the entries, each a
  picture as above. 46 of them: the ball, fireball, bats (three sizes, plain and sticky), bonus
  icons (all stored bottom row first), the brick sheet (bombs, bonus bricks, cracks), explosions, debris, gun mounts, menu icons,
  the title picture.
- **Sounds** (`media/sfx`): the same layout, 47 RIFF WAVs (16-bit PCM, 22–32 kHz), with no names.
  `game.c` picks one for each event; `sounds.txt` in the card folder can change them.
- **Levels** (`gamedata/levels/levels`): 40 records of 216 bytes from byte 2, little-endian words:
  5 rings × 16 bricks, the rings' turning speeds, then the time to beat (word 95) and the ball's
  speed and top speed (98, 104). A brick is `type << 13 | 0x800 offset flag | offset << 5 | colour << 2`
  (a bonus brick's low five bits are its bonus). Types: 1–3 hits, 4 bonus, 5 unbreakable,
  6 exploding (whole rings of them sit in front of everything in some levels, so they must go).
  A flagged brick sits off its ring's middle by (offset − 16) / 32 of the ring spacing.
- **Words** (`Localization/*/text.strings`): `"id"="text";`. The English file is read.
- **Fonts**: only their metrics are in the data files; the glyphs must be in the executable, so
  the game draws its own vector font.

## What it doesn't do yet

- **Music.** The iPod game's three tunes are AAC (`.m4a`), and there is no AAC decoder on the Q2
  for a card game to use, so only the sound effects play.
- The sounds have no names in the bank, so which plays for what is a best guess. Options → sound
  test plays them by number; `sounds.txt` (`bat=5`, one a line) changes the pick.
- Name entry and the other languages' words.
