<img src="ps3/ICON0.PNG" alt="res-PS3 icon" width="240">

# res-PS3

**reLCS (GTA Liberty City Stories) for the PlayStation 3**, as homebrew built
with PSL1GHT.

reLCS (reStories) is the reverse-engineered source code of GTA Liberty City
Stories. This port runs it natively on the PS3, with a hardware renderer on
the RSX (a PS3 backend for librw), DualShock 3 controls and the game's data
converted from your own copy. It is the sibling of
[re3-PS3](https://github.com/stingziz0u/re3-PS3) (GTA III) and
[reVC-PS3](https://github.com/stingziz0u/reVC-PS3) (GTA Vice City), and
shares their PS3 layer.

> **Status: 1.1 alpha.** The game is playable on a real PS3, but it hasn't
> been played through yet. reLCS itself is still a work in progress (its
> audio, for one), and this is still an alpha of the port: **expect bugs,
> crashes and odd things**, and please report them with the log (see
> [Troubleshooting](#troubleshooting)).
> Only tested on real consoles, not on RPCS3.
>
>
> **Known issues:**
> - Car headlights look square.
> - Some LOD buildings can still pop in at a distance.
> - Holding O while aiming with fists repeats very fast punches (not yet
>   checked against the original).
>
> If the game crashes or freezes, please send the log.

**No game data is included.** You need a legally purchased copy of GTA
Liberty City Stories for PS2, converted with the
[reLCS Asset Converter](https://github.com/knackers4/res/releases/download/relcs/reLCSAssetConverter.exe)
(see [Installation](#installation)). The PKG only has the program, its icon
and two res-PS3 files (the PlayStation button icons and the controller
picture of the settings page).

## What's new in 1.1

- **The Cell's SPUs at work**: the audio mixer runs on one SPU and the
  particles' sprites (smoke, sparks, explosions, rain, splashes) are built on
  another, so the PPU has that time for the game. With explosions and rain
  around, the frame rate drops less and recovers faster (in the tests the
  mixer alone took up to 8 ms of the PPU per frame; now 0.3).
- The game's busiest code (collision, physics, entities, animation, the
  world, the render list and the RSX pipelines) is built with `-O3`.
- **Ped voices**: every LCS civilian speaks with its own voice bank (no more
  generic male voice for women and old people).
- Peds that a firefighter mission sets on fire no longer end up under the
  street.
- The taxi passenger no longer screams as if held hostage when getting out
  at the end of a fare.
- `perf.txt` performance test (see [Troubleshooting](#troubleshooting)).

## Features

- Native 720p, rendered on the RSX: tiled colour/depth buffers, compressed
  depth and ZCULL.
- **30 fps** by default (the rate the game was made for) or **60 fps**:
  *Options > Graphics > Frame rate*. Always in sync with the TV (no tearing).
  At 60 fps the vehicles handle as at 30, and the scripts' per-frame timers
  (the taxi tip bar and the like) run at the speed they were made for.
- **PS2 lighting** (*Options > Graphics*, on by default): the buildings lit as
  on the PS2/PSP, with the time of day (prelight x the time cycle's ambient).
  Off: as the PC renderers draw them.
- **Colour filter** (on by default) and **Gamma** 0.8 to 1.6 (*Options >
  Graphics*), for the game's dark nights.
- Controls with lock-on aiming, pad setups, inverted look, free camera,
  third and first person sensitivity, 20% to 200% in steps of 5, and stick
  dead zones, 5% to 70%, in *Controller setup > Gamepad settings > Stick
  settings*; vibration in *Gamepad settings*. **Pressure-sensitive**
  accelerator and brake, as on the PS2 (DualShock 3; *Gamepad settings >
  Pressure sensitive*). PlayStation buttons in the on-screen help.
- **Collectibles on the map** (*Display > Hidden packages / Rampages / Unique
  jumps on map*, each off by default): hidden packages in lime green,
  rampages in blood red and unique jumps (at the take-off) in orange, on the
  radar and the pause menu map. One you pick up, pass or jump disappears.
  The jumps come from the original `main.scm`: with a modified one they
  aren't shown.
- **Car spawner** (for testing, off unless there is a file named
  `carspawner.txt` in `USRDIR`, any content): L3 + R3 together on foot. The
  vehicle shows as "ID: name" over the subtitles; Left/Right go one by one,
  L1/R1 by 10 (hold to scroll), X puts you in it, O closes.
- **MP3 player** radio station: put `.mp3` files in `USRDIR/mp3`; it is in
  the radio dial between the last station and off.
- Ped and car density sliders in *Display*.
- *Options > Graphics*:
  - **Mipmaps**: textures that come without smaller versions get them (less
    shimmering in the distance). They are made as textures load: restart
    the game for all of them. Uses more video memory.
  - **Texture filtering**: Default (the game's own), Bilinear, Trilinear,
    Anisotropic 2x to 16x (sharper ground and roads at an angle). With
    Mipmaps off, only the textures that have them in their TXD use them.
  - **Antialiasing**: Off or FXAA (smoother edges, a filter over the
    finished picture: ~2 ms of GPU time and 7 MB of video memory at 720p).
- DXT textures stay compressed in video memory and the RSX reads them as
  they are. Mip levels made on the CPU use AltiVec.
- Two of the Cell's SPUs: the audio mixer and the particles' sprites (see
  [What's new in 1.1](#whats-new-in-11)).
- *Display > Screen size* for overscan, FPS counter (*Graphics > Show FPS*).
- Settings saved in `USRDIR/reLCS.ini`, saves in `USRDIR/userfiles`.

## Changes from the original

The PS2/PSP game was tuned for those consoles' memory and frame rate: a few
things are hard for that reason alone. res-PS3 changes these (everything
else plays as the original):

- **Rampages**: while one runs, the ped and car limits no longer depend on
  the density sliders (at least half of their range), the gang or the
  vehicles of the rampage are asked for and make up half of what appears,
  so there are enough targets.
- **60 fps**: vehicle rotation damping and the scripts' per-frame steps are
  scaled to the frame time, so they behave as at 30 fps.

## Installation

1. Convert your copy of GTA Liberty City Stories for PS2 with the
   [reLCS Asset Converter](https://github.com/knackers4/res/releases/download/relcs/reLCSAssetConverter.exe)
   (on a PC).
2. Install `res-ps3-1.1-alpha.pkg` on a PS3 with homebrew support (CFW or
   HEN), e.g. copy it by FTP and install it with the package manager. It
   appears in the XMB as **reLCS**.
3. The PKG creates `/dev_hdd0/game/RESPS3000/USRDIR` with these empty
   folders. From the converter's output, copy into them (by FTP):

   | Folder  | Contents                                   |
   |---------|--------------------------------------------|
   | `anim`  | the converted `anim` folder (`ped.ifp`, `cuts.img`, `cuts.dir`...) |
   | `audio` | the converted `audio` folder (`sfx.raw`, `sfx.sdt`, the streams) |
   | `data`  | the converted `data` folder (`gta_lcs.dat`, `main.scm`, `timecyc.dat`...) |
   | `models`| the converted `models` folder (`gta3.img`, `gta3.dir`...; keep the two res-PS3 `.txd` already there) |
   | `text`  | the converted `text` folder (`american.gxt`...) |
   | `txd`   | the converted `txd` folder (loading screens) |
   | `mp3`   | optional: your own music                   |

   Filenames are case-sensitive on the PS3: keep them lowercase, exactly as
   above. The log checks the main files at start (`[4]` lines) and says
   which one is missing.
4. Start **reLCS** from the XMB.

## Mods

**`USRDIR/mods`**, like a mod loader on the PC: a file put there (in any
subfolder, any letter case) is used instead of the game's file with the same
name, whether that one is loose (`handling.cfg`, `main.scm`...) or inside
`gta3.img`. Nothing has to be rebuilt: take a mod's files out of its folder
or delete them and the original ones are back. If two files have the same
name, the one whose path comes first in alphabetical order is used. The log
lists every file replaced (`[mods]` lines). Files in `USRDIR` itself
(`reLCS.ini`, settings, logs), `userfiles` and `mp3` are never replaced.
`.img` files inside `mods` aren't read: take the files out of them with an
IMG tool.

Data files and textures/models in the formats reLCS reads work. Code mods
and unconverted assets from other games or platforms don't.

## Troubleshooting

- The log is `USRDIR/res-ps3.log` (the previous one: `res-ps3.old.log`).
  Send it with any report: it says what the game was doing when it crashed.
- A black screen at start: create an empty `USRDIR/notiles.txt` (turns off
  the tiled buffers), and report it.
- A texture that looks wrong: create an empty `USRDIR/nodxt.txt` (DXT
  textures converted on the CPU), and report it. If that isn't it, an
  empty `USRDIR/notexthread.txt` converts the textures on the game's own
  thread again; `USRDIR/novmx.txt` makes the mip levels without AltiVec;
  and `USRDIR/dxt32.txt` keeps converted DXT1 textures in 32 bits instead
  of 16.
- Buildings popping in or out: an empty `USRDIR/nolodpairs.txt` goes back
  to the game's own rule for the LOD buildings, `USRDIR/noocclusion.txt`
  turns off the occluders. Report which one changes it, with the log.
- Sound or particles that look or sound wrong: an empty
  `USRDIR/nospuaudio.txt` mixes the audio on the PPU again,
  `USRDIR/nospuparticles.txt` builds the particles on the PPU again, and
  `USRDIR/spuparticles_ram.txt` keeps them on the SPU but has it write to
  main memory (the PPU copies them for the RSX). Report which one changes
  it, with the log.
- `USRDIR/perf.txt` runs a performance test (load a save outdoors, not on a
  mission, and leave the pad alone; it starts 10 s later and takes about 6
  minutes in Portland): it measures every frame at a few busy places, clear,
  in the rain, with explosions and both, switching the SPU particles off and
  on, and writes `[perf]` lines to the log. `secs=N` in the file sets the
  seconds per run (4 to 30). It is renamed to `perf.done.txt` at the end.
- `USRDIR/autotest.txt` runs an automatic test while you watch (load a save
  outdoors first). The file is renamed to `autotest.done.txt` when the test
  ends.

## Building

On Linux or WSL (Ubuntu):

1. Install the PS3 toolchain and PSL1GHT (https://github.com/ps3dev/ps3toolchain),
   which ends up in `/usr/local/ps3dev`.
2. Build:

   ```sh
   git clone https://github.com/stingziz0u/reLCS-PS3.git
   cd reLCS-PS3
   export PS3DEV=/usr/local/ps3dev PSL1GHT=/usr/local/ps3dev
   export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH
   make -C ps3 -j$(nproc)
   make -C ps3 verify
   ```

   The result is `ps3/res-ps3-1.1-alpha.pkg`.

Other targets: `make -C ps3 DEBUG=1` (debug build with asserts),
`make -C ps3 STAGE=1` (headless test build: loads the game without video,
audio or pad, everything to the log), `make -C ps3 clean`,
`make -C ps3 crash LOG=res-ps3.log` (turns a crash's addresses in a log into
functions and lines).

The RSX shaders are in the tree already compiled
(`vendor/librw/src/ps3/ps3shaders.h`). To change them you need NVIDIA's `cgc`
and PSL1GHT's `cgcomp` with `cgcomp-relative-constants.patch`; see
`vendor/librw/src/ps3/shaders/build_shaders.sh`.

### Where things are

- `ps3/` — Makefile, PKG icon, patch level, tools (`lcsdis.py`, a
  disassembler for LCS's `main.scm`; `crashaddr.py`).
- `src/skel/ps3/` — platform layer: main loop, file system, pad, log, test.
- `src/audio/sampman_ps3.cpp` — audio (mixer on an SPU, or the PPU, on libaudio; MP3/WAV streams).
- `ps3/spu/` — the SPU programs: the audio mixer (`spumix.c`) and the
  particles' sprites (`spuparticles.c`), built with `spu-gcc` and put in the
  EBOOT.
- `vendor/librw/src/ps3/` — librw's RSX backend: VRAM, rasters, pipelines
  (the Leeds world pipeline for the PS2 lighting), shaders.
- `src/core/config.h` (`__PS3__` section) — what the PS3 build turns on and off.

## Credits

- The reStories team for reLCS, the [re3 team](https://github.com/GTAmodding/re3)
  for re3/reVC, on which it is built, and aap for [librw](https://github.com/aap/librw).
- The [ps3dev](https://github.com/ps3dev) people for ps3toolchain and PSL1GHT.
- lieff for [minimp3](https://github.com/lieff/minimp3).

The original reLCS README is kept as [README.res.md](README.res.md).

## License

Grand Theft Auto: Liberty City Stories is © Rockstar Games / Take-Two
Interactive. This project is not affiliated with or endorsed by Rockstar
Games, Take-Two Interactive or Sony Interactive Entertainment.

## AI disclosure

This project's code was written collaboratively with Claude (Anthropic),
working through this port with me in real time over many sessions. Every
bit of testing and debugging was done by me on real hardware.
