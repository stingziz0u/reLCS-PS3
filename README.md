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

> **Status: 1.4 alpha.** The game is playable on a real PS3, but it hasn't
> been played through yet. reLCS itself is still a work in progress (its
> audio, for one), and this is still an alpha of the port: **expect bugs,
> crashes and odd things**, and please report them with the log (see
> [Troubleshooting](#troubleshooting)).
> Only tested on real consoles, not on RPCS3.
>
>
> **Known issues:**
> - Some LOD buildings can still pop in at a distance.
> - Thin dark lines on some buildings flicker as the camera moves (two
>   surfaces at the same depth).
> - The experimental second render thread (`USRDIR/rsplitthread.txt`, off
>   by default) shows LOD pop-in the first time you approach some buildings
>   and isn't faster yet.
>
> If the game crashes or freezes, please send the log (it is only written
> with an empty `USRDIR/logson.txt`, see [Troubleshooting](#troubleshooting)).

**No game data is included.** You need a legally purchased copy of GTA
Liberty City Stories for PS2, converted with the
[reLCS Asset Converter](https://github.com/knackers4/res/releases/download/relcs/reLCSAssetConverter.exe)
(see [Installation](#installation)). The PKG only has the program, its icon
and two res-PS3 files (the PlayStation button icons and the controller
picture of the settings page).

## What's new in 1.4

Performance (in the tests the busiest streets of Portland stay at or near 60 fps):

- **Faster collision**: every building keeps its collision box in the
  world, so the lines the game casts (the cars on rails put on the road,
  the camera, the lines of sight) skip the buildings they can't touch
  before any maths; the big collision models have a grid of their
  triangles, so a car only looks at the triangles near it (about 3000
  boxes a frame instead of 28000). Collision went from about 2.5 ms a frame
  to 1.5, and the cars on rails from 0.8 ms to 0.2-0.4. Both give the same
  results as before (`USRDIR/nocolgrid.txt` turns them off).
- The lines of sight and vertical lines go through the triangles' boxes
  too (only the triangles their box touches are tested).
- The sound service runs on its own thread while the scene is drawn
  (`USRDIR/noaudiothread.txt` turns it off), and the radio's MP3 files are
  opened on the stream thread (they stalled the frame).
- The peds' bones are worked out on an SPU (`USRDIR/nospuanim.txt` turns
  it off).
- Drawing: the opaque buildings sorted by texture, the sampler registers
  and the shader constants only written when they change, lean flushes for
  a building's later meshes, the off-screen LODs culled by their sphere.
- No pause when crossing to another island.

Fixes:

- **The windows that came and went** (glass2_64): depending on the weather
  some windows showed the empty inside of the building or vanished. The
  buildings' see-through materials are drawn at full alpha, as after their
  fade-in (`USRDIR/glassalpha.txt` goes back to their own alpha).
- **Saves with bikes**: the LoadVehiclePool / SaveVehiclePool asserts are
  gone (the saved bikes' size was a boat's).
- **The MP3 player** goes on where it was, starts on a random song, no
  longer restarts while you tune the radio, and the radio no longer plays
  the save's songs every time.
- Blips: the script's colours and LCS's colours for peds and places, *The
  Sicilian Gambit*'s wise guy keeps his blip, and a blip a mission left in
  the save goes away.
- The rampages show up on the map again (the on-a-mission flag stuck
  at 1).
- Interiors: the locked camera no longer spins, the camera on foot works as
  the free one, a script camera indoors gets the 4:3 view, Ammu-Nation's
  camera no longer clips through and its display guns stay loaded,
  Toshiko's flat shows in her cutscenes.
- The subway seen from its platforms, the LODs paired by place, graffiti
  drawn after their walls, the LODs pushed behind the detailed buildings.
- The ferry out of the car spawner; full health and armour mean the max.

Experimental (off by default, each an empty file in `USRDIR`):

- `rsplitthread.txt`: the buildings recorded by a second thread into
  command buffers the RSX calls, while the game thread draws the rest.
  Stable in the tests, but with LOD pop-in the first time you approach
  some buildings, and the PPU's two hardware threads share one core, so
  it isn't faster yet. Normal play doesn't need it: every fix and speed-up
  above is in without it.
- `rsplit.txt`: the buildings' draws into a command sub-buffer on the same
  thread.

## What's new in 1.3

The streets of Liberty City, drawn right:

- **No more dark wedges on the roads**: the dark triangles that covered
  parts of some streets (and came back at a distance) were two road LODs
  drawn over the detailed road. Those two now go by the distance to their
  bounds, so the detailed road is drawn as you get near
  (`USRDIR/noboundlod.txt` goes back to the old rule).
- **The Staunton road shadows** no longer vanish or flicker: the models
  that don't write the depth are drawn after the opaque world, the alpha
  list is four times bigger, and buildings are put on screen by their drawn
  geometry when it is bigger than their collision.
- **No more pitch black buildings in the rain at night**: buildings without
  prelight are lit by the time cycle's ambient, and the emission has a
  floor.
- **Fog in the rain** (no pop-in at the draw distance, no black LODs),
  and the LODs are kept until the fog hides them.
- Buildings take the textures their own TXD doesn't have from any loaded TXD
  that has them (the white or missing world textures).
- Models fade in over LCS's half second at 60 fps too.
- **Toni's phone**: it comes out and stays in his hand, and his head and
  arm move as they should during the call.

Missions:

- *Avenging Angels*: the gang members' script no longer stops (a missing
  return), and the frame drops around them are gone.
- A bike you get on no longer starts with a bomb (the Pizza Boy that blew
  up).
- The race's "go", its end and the other front-end sounds play without a
  wrong sample on top.

Performance:

- Fires no longer drop frames (a static shadow that casts nothing isn't
  cast again every frame); moving static shadows, car smoke and the rain
  (drops, splashes, steam, streaks) run on 30 ticks a second at 60 fps.
- At most 24 medium explosions alive at once, the helicopter's dust capped,
  160 coronas (street lamps only close), 1600 shadow polygon bunches and
  128 static shadows (lamp light pools to 80 m).

From Gamer100Dev's [reVC-PS3-Optimised](https://github.com/Gamer100Dev/reVC-PS3-Optimised):

- Shadows filtered right, the ped shadow never wraps to black at the edge
  of its range, the car lights fade smoothly, the long corona streaks
  without their square.
- The drops and blood on the lens refract this frame, not the one before.
- The radio tunes a station 8 frames after the last press instead of 20.
- RSX safety: the waits for the RSX keep waiting instead of reusing memory
  still in use, a full vertex ring starts over instead of dropping draws,
  and damaged textures are refused.

Also:

- **The rocket launcher's view over the whole screen**, as in San Andreas
  (the black frame around it is gone; the sniper rifle keeps its scope).
- **Mission spawner** (`USRDIR/missionspawner.txt`): every mission and side
  activity from a list (see [Features](#features)).
- `USRDIR/weather.txt` with a number forces a weather (see
  [Troubleshooting](#troubleshooting)).
- Test modes for the reports, only with the log on (R3 + Select): hide the
  ground or light effects, the model or mesh under the screen centre, a
  free camera over the frozen view with a cross to pick a model, a log of
  what is drawn frame by frame and every LOD hidden.

## What's new in 1.2

- **Smoother streaming**: driving around the city now holds its frame rate
  (in the tests, minutes in a row without a single frame over 20 ms at
  60 fps).
  - The game data is read on a thread of its own: the game no longer
    stops and waits for the disk whenever a new area or car loads (8-18 ms
    each time before).
  - The TXDs' true colour textures go straight into the PS3's textures,
    finished by the texture thread on the PPU's second hardware thread:
    a car or building TXD took 6-14 ms of the game's frame, now 1.5-2.6.
  - Toni's lines and the radio and music changes are opened by the audio
    stream thread (they stalled the frame for 9-15 ms).
- **Faster collision**: a first pass with AltiVec over the triangles'
  bounding boxes skips the exact tests that can't hit.
- **The log is off by default**: nothing is written to the disk while you
  play. An empty `USRDIR/logson.txt` turns it on (see
  [Troubleshooting](#troubleshooting)).
- **Melee lock-on**: bats, knives, the chainsaw and the other melee weapons
  lock on like the fists. Toni moves freely while locked on and the camera
  stays on the target; a swing plays out (no frozen aiming pose).
- Locked on with the fists, one attack per press (no endless fast punches).
- The lock-on camera also goes behind Toni at the farthest camera zoom.
- The fire truck's water cannon (and the Rhino's turret) aim with the right
  stick all the time, with or without the free camera.
- No more square car headlights.

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
- **Mission spawner** (for testing, off unless there is a file named
  `missionspawner.txt` in `USRDIR`, any content): L3 + Select together. Every
  mission and side activity (races, Bumps and Grinds, the RC and vehicle
  jobs...) shows as "number: name [script]"; Left/Right go one by one, L1/R1
  by 10 (hold to scroll), X starts it, O closes. A mission that is running
  ends first; Toni is taken to where the chosen one starts, in its vehicle if
  it needs one.
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
  [What's new in 1.1](#whats-new-in-11)). The PPU's second hardware thread
  converts textures and reads the game data (see
  [What's new in 1.2](#whats-new-in-12)).
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

- **The log is off by default** (nothing is written to the disk while you
  play). To turn it on, create an empty `USRDIR/logson.txt`; the log is then
  `USRDIR/res-ps3.log` (the previous one: `res-ps3.old.log`). Send it with
  any report: it says what the game was doing when it crashed. The tests
  below need it on.
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
  Also (each an empty file): `noboundlod.txt` (the road LODs by their
  pivot again), `nolodfog.txt` (the LODs hidden at their draw distance,
  not the fog), `norenderbounds.txt` (buildings on screen by their
  collision only), `nowindfix.txt` (the meshes' triangles as the strips
  give them).
- Buildings too dark or too bright: an empty `USRDIR/nounlitlift.txt` or
  `USRDIR/noemissfloor.txt` turns off the light added to the buildings
  without prelight or the emission floor.
- `USRDIR/weather.txt` with a number in it (0 sunny, 1 cloudy, 2 rainy,
  3 foggy, 4 extra sunny, 5 hurricane) keeps that weather all the time
  (it isn't saved in the game).
- With the log on, R3 + Select goes through the test modes (the name shows
  on screen); mode 0 is the normal game.
- Something not loading, or loading late: an empty `USRDIR/nocdthread.txt`
  reads the game data on the game's own thread again, and
  `USRDIR/notxddirect.txt` converts the TXDs' true colour textures the old
  way. Radio or music that misbehaves when changing stations:
  `USRDIR/nostreamasync.txt` opens the streams on the game's thread again.
  Collisions that look wrong: `USRDIR/novmxcol.txt` turns off the AltiVec
  collision broadphase, and `USRDIR/nocolgrid.txt` the collision grids and
  the buildings' world boxes (`USRDIR/vmxcolcheck.txt` compares the
  broadphase with the old loop and logs any difference). Sound that
  stutters or stops: `USRDIR/noaudiothread.txt` runs the sound service on
  the game's thread again.
- Sound or particles that look or sound wrong: an empty
  `USRDIR/nospuaudio.txt` mixes the audio on the PPU again,
  `USRDIR/nospuparticles.txt` builds the particles on the PPU again, and
  `USRDIR/spuparticles_ram.txt` keeps them on the SPU but has it write to
  main memory (the PPU copies them for the RSX). Peds that move wrong:
  `USRDIR/nospuanim.txt` works their bones out on the PPU again. Report
  which one changes it, with the log.
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
- Gamer100Dev for the fixes of
  [reVC-PS3-Optimised](https://github.com/Gamer100Dev/reVC-PS3-Optimised).

The original reLCS README is kept as [README.res.md](README.res.md).

## License

Grand Theft Auto: Liberty City Stories is © Rockstar Games / Take-Two
Interactive. This project is not affiliated with or endorsed by Rockstar
Games, Take-Two Interactive or Sony Interactive Entertainment.

## AI disclosure

This project's code was written collaboratively with Claude (Anthropic),
working through this port with me in real time over many sessions. Every
bit of testing and debugging was done by me on real hardware.
