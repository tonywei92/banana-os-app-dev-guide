# Making an app for BananaOS on the RG DS and RG DS Plus

A field guide for writing a native app for [BananaOS](https://github.com/JorreFog/BananaLinux) on the Anbernic RG DS
and RG DS Plus, and the starter in this folder that follows it. Everything here was learned building a real app, a
media player for these handhelds, and checked on the device. Each rule carries its reason, so you can tell when it
applies to you.

**The hardware:** an RK3568 (4x Cortex-A55, aarch64, Mali-G52 GPU), about 1 GB of RAM and no swap. Two 4:3 panels,
one above the other: the top one is the main screen, the bottom one is a touchscreen. The RG DS's panels are 640x480,
the RG DS Plus's 1024x768. The buttons are Nintendo-style: A B X Y, a D-pad, L R L2 R2, Start, Select, F (a function
key), volume keys, power, and one analog stick (the left), which clicks (L3). There is no right stick. A lid with a hall
sensor, a headphone jack, speakers, Wi-Fi and Bluetooth.

## Quick start

```sh
# a computer (macOS: brew install sdl2 sdl2_ttf cmake pkg-config); two windows stand in for the two panels
cmake -S . -B build && cmake --build build -j
./build/app-test                       # the pure parts' tests, a fraction of a second
./build/my-app                         # arrows, Enter = A, Esc = B, X, Tab = Select, S = Start, F; the mouse on the bottom window is a finger

packaging/build-bundle.sh --quick      # aarch64 build in Docker -> dist/ports/MyApp
tools/push-handheld.sh root@<handheld-ip>   # onto the handheld over SSH; then open it from the shelf
packaging/build-bundle.sh              # the release zip: dist/MyApp-<version>-aarch64.zip
```

Rename it: `APP_ID` in `src/applog.h`, the `project()` name and `my-app` target in `CMakeLists.txt`, the four
variables at the top of `packaging/build-bundle.sh`, the names in `packaging/launch.sh`, `packaging/port.sh`,
`tools/push-handheld.sh` and `packaging/store/catalog-entry.json`.

What's in the starter:

| File | What it does |
| --- | --- |
| `src/main.cpp` | SDL, both panels, the event loop, signature redraws, the post-start/wake refresh, the demo |
| `src/input.*` | the buttons mapped to what is printed on them, the triggers made buttons |
| `src/touch.*` | the bottom touchscreen read from evdev (Linux only) |
| `src/sleepwatch.*` | the lid (`SW_LID`) and SIGCONT after a sleep |
| `src/lidsleep.*` | keep running with the lid shut: BananaOS's lid sleep turned off only while needed, and put back |
| `src/levels.*` | volume, brightness, the bottom backlight, battery, through the allowed interfaces |
| `src/applog.*` | the state folder, `log.txt`, `crash.log` |
| `src/child.*` | a helper process (a JVM...) that never outlives the app (not used by the demo) |
| `packaging/launch.sh`, `port.sh` | the Store's launcher and the Ports shelf's entry |
| `packaging/build-bundle.sh` | the aarch64 build and bundle in Docker, cached |
| `packaging/store/` | the Store's catalog entry template and its 32x32 icon |
| `tools/push-handheld.sh` | changed files onto the handheld over SSH, a clean quit, a folder swap |

## How apps launch

- **Two ways in, one folder.** A **Store app** is started by BananaOS's `run-store-app`, which runs the catalog
  entry's `menu.launch` (`MyApp/launch.sh`). A **Port** is a script on the Ports shelf, `roms/ports/My App.sh`, run by
  `run-port`; the starter's `port.sh` only execs `MyApp/launch.sh`. *Why one launcher:* the zip installs both ways and
  an update of the folder updates both.
- **The environment** both give the app: its own session, `HOME=/data/home`, `XDG_CONFIG_HOME=/data/home/.config`,
  and `SDL_KMSDRM_FIRST` set to the top panel's connector (`DSI-2`), so the top panel is SDL display 0. `run-store-app`
  leaves both panels lit; `run-port` darkens the bottom backlight (ports usually draw on the top panel only), so
  `launch.sh` turns it back on if your app uses it. *Why it matters:* without that, your bottom panel is black on the
  Ports shelf and fine from the Store.
- **The state folder** is `$XDG_CONFIG_HOME/<app-id>` (`/data/home/.config/myapp`, `home/.config/myapp` on the card).
  *Why there and not in the app's folder:* an update replaces the folder whole, a removal deletes it, and the Store and
  Ports installs share this one.
- **Logs.** The app writes `<state>/log.txt` itself and keeps the previous run's as `log.prev.txt` (`applog.cpp`);
  `launch.sh` writes `<state>/launcher.txt` with the exit status. *Why both:* there is no terminal on the handheld, a
  relaunch after a problem must not wipe the run that had it, and a missing library fails before `main()` can open
  any log, so only the launcher sees it. A user can send you these files from the card.
- **The launcher waits for the app in the background** with a trap on TERM/INT/HUP that passes the signal on. *Why:*
  bash runs a trap only between commands, so a foreground child would hide the session's TERM from it.

## The two panels

- **On the handheld SDL's video driver is KMSDRM**, each panel is one SDL display, and each window fills one
  (`SDL_WINDOW_FULLSCREEN_DESKTOP`). *Why no X or Wayland:* there is none on BananaOS; SDL draws straight to the panels.
- **The top panel is display 0** thanks to `SDL_KMSDRM_FIRST`; the starter also matches the display name against
  `DSI-2` (`BANANA_TOP` overrides it). *Why both:* a launch without the variable (from SSH) would otherwise swap them.
- **Lay out in 640x480 units** on both panels and scale to the real size (`Panel::scale` in `main.cpp`), rendering text
  at the real size. *Why:* the RG DS is 640x480 and the RG DS Plus 1024x768, both 4:3; one layout fits both and text
  stays sharp.
- **KMSDRM drops the first frames presented after the start and after a wake.** A panel drawn once and then left alone
  can stay black. So the starter draws both panels again every 100 ms for 8 s after the start, 2 s after a wake and
  0.5 s after an expose, and every 2 s at all times (`freshUntil` in `main.cpp`). *Why 8 s at the start:* the first launch
  after boot is slow (shaders compile afresh), and a picture that first shows after 2 s would never be presented again.
- **`SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS=0`.** *Why:* with two windows, focus moves between them; a minimised panel
  is a black panel.

## Input

- **Nintendo layout.** SDL names face buttons by position, Xbox-style: the handheld's **A (east) arrives as
  `SDL_CONTROLLER_BUTTON_B`**, and B (south) as SDL's A. **X and Y arrive under their printed names** (not swapped).
  *Why it matters:* map by position and your "press A" hint answers to B.
- **L2 and R2 are trigger axes** (`SDL_CONTROLLER_AXIS_TRIGGERLEFT/RIGHT`, 0..32767), not buttons. The starter makes them
  buttons: down past half, up again only below a quarter. *Why the two lines:* a trigger resting near one line would
  press over and over.
- **L3** is `SDL_CONTROLLER_BUTTON_LEFTSTICK`; the stick is `LEFTX`/`LEFTY`. There is no right stick on the RG DS Plus.
- **F is SDL's `GUIDE`, and bananad shares it:** F + volume keys is BananaOS's brightness. Act on F alone only as it is
  let go, and only if nothing else was pressed while it was held. *Why:* otherwise every brightness change also opens
  your menu. The starter quits on F + Start.
- **The volume keys are bananad's.** It changes the level itself and writes it to `/run/banana/osd`
  (`volume 85`, `brightness 40`, the file renamed into place). Watch that file (inotify) if you want to show a level
  card. *Why not handle the keys:* the level would move twice.
- **The Back key** arrives as `SDLK_AC_BACK` (taken as B; unconfirmed on every unit). Log each press (`input: ...` in
  the starter) so a user's log shows what their buttons sent.
- **Open pads on `SDL_CONTROLLERDEVICEADDED`.** *Why:* SDL sends it for the built-in controls at start too, and a
  controller connected later is handled by the same code.

## Touch

- **The bottom touchscreen is read straight from evdev**, found by its place in sysfs: the input device whose
  `/sys/class/input/eventN/device` resolves to a path containing `fe5e0000.i2c` (its I2C controller; `BANANA_TOUCH`
  overrides it). *Why not SDL:* the handheld's touch devices can share a name and SDL doesn't say which panel a finger
  is on; BananaOS's own menu finds it the same way. Never use the `eventN` number: it changes between boots.
- **The reader is a thread** that turns each `SYN_REPORT` into Down/Move/Up with x, y from 0 to 1 and pushes an SDL user
  event (`touch.cpp`). *Why `SYN_REPORT`:* X and Y of the same moment arrive as separate events; only the report says
  they belong together.
- **With your own reader running, turn SDL's touch-to-mouse off** (`SDL_HINT_TOUCH_MOUSE_EVENTS=0`) and hide the
  cursor. *Why:* SDL still reads the touchscreens, and on KMSDRM each touch moved a cursor plane, a commit that can
  wait for the next flip inside `SDL_PollEvent`: input stalls.
- **On a computer** the evdev reader finds nothing and the mouse on the bottom window stands in.

## Lid and sleep

- **bananad sends the app's whole process group SIGSTOP** when the handheld sleeps (the lid closing with lid sleep on,
  or the power key) **and SIGCONT when it wakes.** There is no warning before the stop. *Why it matters:* audio,
  timers and network calls simply resume where they were, as if no time had passed.
- **Pause on the lid's `SW_LID` event** (read from evdev, the device found by its `EV_SW` capability) **and again on
  SIGCONT** (`sleepwatch.cpp`). *Why both:* the lid's event usually arrives before the stop, but not always; SIGCONT
  always comes after a sleep. *Why not resume on open:* the user opens the lid, then decides.
- **With lid sleep off** (a BananaOS setting), closing the lid only darkens the panels and nothing is stopped: the lid's
  event is all you get.
- **The SIGCONT handler only writes a byte to a pipe.** *Why:* `SDL_PushEvent` isn't async-signal-safe; a thread
  reads the pipe and pushes the event.
- **After a wake, read the lid's state with `EVIOCGSW`** rather than trusting events. *Why:* events can be lost across a
  stop.

## Keep running with the lid shut

For an app that must go on in a pocket (music, a download): BananaOS's lid sleep, turned off only while needed
(`src/lidsleep.*`).

- **What bananad reads:** `lid_sleep` from `/etc/banana/bananad.conf`, then `/data/banana/bananad.conf` (`key = value`;
  a line starting with `#`, or without `=`, is skipped; key and value trimmed; `yes` or `1` is on; **the last line
  wins**). On by default. It reads them at start and on **SIGHUP** only. BananaOS's Settings writes the `/data` file,
  which need not exist.
- **With `lid_sleep` on, closing the lid suspends at once**, whatever runs. With it off the lid only turns the panels
  off. If lid sleep comes back on while the lid is still shut, bananad sleeps after 5 s without input.
- **The sanctioned sleep:** create `/run/banana/sleep-now`, then SIGHUP bananad (found by name in `/proc`; it has no
  pid file). Its reload removes the file and sleeps. *Why not `/sys/power/state`:* bananad would not know, and its
  wake handling (SIGCONT, backlights, the lid) would be skipped. One SIGHUP covers a conf change and the sleep.
- **The rule** (`LidGuard` in `lidsleep.cpp`, ticked every pass with "does the app need to stay awake now"):
  1. **Write `lid_sleep = no` only while it's needed**, and only if lid sleep is on. *Why:* it's the user's setting;
     you borrow it.
  2. **Write the note first** (`<state>/lid-restore`: what was changed, the old line, the line written), then the
     conf, each as a temp file, `fsync`, rename; then SIGHUP. *Why the note first:* the power can go at any moment;
     the note is what lets the next start undo it.
  3. **Change only the effective line** (the last `lid_sleep`), or append one, keeping every other byte (comments,
     a missing final newline). *Why:* it is the user's settings file.
  4. **Undo only if the line is still byte for byte yours.** *Why:* if it differs, the user changed it in BananaOS's
     Settings meanwhile, and their choice wins.
  5. **Undo after a short grace** (3 s) once it's no longer needed, **at once on quit**. *Why the grace:* a gap between
     two tracks is not the end; an undo there with the lid shut would sleep the handheld mid-playlist.
  6. **Undo a leftover note at start** (power loss, SIGKILL), before anything else; `launch.sh` also runs
     `my-app --lid-restore` after any exit, so a crash is covered by the same code.
  7. **With the lid shut when you're done, ask for the sleep yourself** (`sleep-now` + SIGHUP). *Why:* otherwise the
     handheld stays awake in a pocket until the lid is opened and closed again.
- **The headphone jack** is `SW_HEADPHONE_INSERT` on an input device (found by capability, like the lid; 1 -> 0 is an
  unplug). **Bluetooth headphones:** `/run/banana/bt/audio` (`<MAC> <PCM>`) exists while A2DP headphones are connected
  and bananad routes sound to them. Pause on either going away. *Why:* otherwise the sound moves to the speakers in
  public. Look at `bt/audio` twice a second apart before calling it gone (it is rewritten from time to time).

## Volume, brightness and the bottom backlight

These are the only interfaces to use (`src/levels.*`):

- **Volume:** `/usr/libexec/banana/volume get` and `set N` (0..100), then SIGHUP to bananad so it stores the level.
  **Never `amixer`.** *Why:* the mixer controls behind the script differ between devices and releases, and bananad
  would not know the level changed. Run the script off the main thread, latest value only (`Volume::set`). *Why:* it
  takes tens of milliseconds; a dragged slider would queue dozens of runs.
- **Brightness:** `/sys/class/backlight/backlight1` is the top panel, `backlight0` the bottom. Write
  `brightness = max_brightness * permille / 1000` (never 0, never under 10%), then both levels as `"<top> <bottom>"`
  thousandths to `/data/banana/backlight`. **No other interface.** *Why:* bananad restores that file at boot and reads
  sysfs at each brightness key, so its keys carry on from your level; a file with 0 or under 10% is thrown away and
  both panels start at the kernel's default.
- **The bottom backlight off/on:** `bl_power` 4/0 on the backlight that `/run/banana/backlight-bottom` names, and
  `/run/banana/panel-off` holding that path while it is off (`BottomPanel`). *Why the marker:* bananad keeps the panel
  dark after a wake only if it sees it. Turn it back on when you quit (and from a crash handler, if you turn it off).

## Battery and sound

- **Battery:** `/sys/class/power_supply`: the first supply of `type` `Battery` gives `capacity` and `status`. **A
  charger online counts as charging.** *Why:* the RG DS's rk817 gauge says "Discharging" while it charges. Read it every
  30 s or so, not every frame.
- **Sound is plain ALSA, with no sound server** (no PulseAudio, no PipeWire). bananad writes the default PCM
  (`/etc/asound.conf`), through a `plug` and an EQ to the speakers' card. Open `default`, ask for stereo (the plug maps
  channels), and let SDL's ALSA backend or your library do the rest. *Why stereo:* a 5.1 stream asked for as 5.1 has
  nowhere to go.
- **The speakers' amplifiers (two aw88166) sometimes fail to start** with a stream: one silent speaker, or none
  (`dmesg | grep aw88166`). Keeping a stream open through pauses (silence) makes starts rarer. *Why mention it:* it
  looks like your bug and isn't.

## What to bundle, what to take from the system

- **Take from BananaOS, never bundle:** glibc, libstdc++, SDL2, SDL2_ttf, Mesa (EGL/GLES), libdrm, ALSA, FreeType, zlib,
  libpng, OpenSSL, libcurl. *Why:* BananaOS's SDL2 is patched (`SDL_KMSDRM_FIRST`); its Mesa drives the GPU and needs
  its own libstdc++; glibc must match the loader; its OpenSSL and curl get its security updates.
- **Bundle everything else** your binary needs, in `lib/` beside `bin/`, found with `LD_LIBRARY_PATH` (launch.sh) and an
  rpath of `$ORIGIN/../lib`. `build-bundle.sh` walks the binary's `NEEDED` entries and copies whatever isn't on the
  system list. A media app, for example, bundles libmpv, FFmpeg, libplacebo, libass, fontconfig, expat, harfbuzz and
  fribidi.
- **Build against an older glibc** than BananaOS's (Debian 12, glibc 2.36). *Why:* a binary runs on a glibc at least
  as new as the one it was linked against, never older.
- **Fonts: BananaOS has none.** Ship yours (`share/myapp/fonts`, with their licences). SDL_ttf has no fallback: a
  character missing from the font draws as a box, so ship fonts for the scripts you show, and pick per run of text.
  If you use fontconfig (libass does), point `FONTCONFIG_FILE` at a `fonts.conf` listing only your folder.

## The GPU, and decoding

- **The Mali-G52 may take no texture over 8192 px a side** (unverified); keep images at 4096 or under. *Why:* a
  texture that fails to upload is a black rectangle with no error on screen.
- **OpenGL ES 2 through Mesa (Panfrost)** under KMSDRM; SDL's renderer uses it. Desktop GL is not there.
- **Video decoding is software only.** The kernel has no `CONFIG_MEDIA_SUPPORT`, so no hardware decoders. *Why it
  matters:* on four A55 cores up to 720p plays smoothly; above that, measure on the device before promising it.

## SDL2, and using it efficiently

**Why SDL2 on this handheld:** it's part of BananaOS (patched, so never bundled), it runs straight on KMSDRM with no
compositor and no X or Wayland, it is tiny next to heavier UI toolkits, and it drives both panels, the controller and
audio from one library.

How to keep it cheap (the starter's `main.cpp` does each of these):

- **An event-driven loop.** `SDL_WaitEventTimeout` with a timeout set to the next thing the loop must do by itself,
  then drain with `SDL_PollEvent`. *Why:* the CPU sits near 0 when idle (the starter uses about 0.4 s of CPU in 10 s on
  a Mac, start included); a busy loop or a fixed 60 fps drains the battery doing nothing. The media player computes
  its wait each pass from what is animating (16 ms while something moves, up to 250 ms idle).
- **Redraw a panel only when what it shows changes:** each panel has a signature (a string of everything drawn) and
  is drawn and presented only when it differs from the last one. *Why:* most of the time nothing changed; a present is
  a GPU flip and a frame of work. New drawing state must go into the signature, or it won't show. (In the player, the
  bottom panel's `Ui::signature()` decides every redraw.)
- **Cache text textures**, keyed by text + colour (+ size). *Why:* `TTF_Render*` (rasterising glyphs) plus the upload
  is by far the costliest thing a 2D page does, and a page shows the same strings redraw after redraw. The starter's
  `Panel::text` keeps up to 256 and empties the cache when it grows past that.
- **No per-frame surface-to-texture uploads.** Make a texture once, reuse it, change it only when its picture changes;
  keep textures under the GPU limit above. *Why:* an upload goes over the memory bus every time.
- **Vsync on** (`SDL_RENDERER_PRESENTVSYNC`). *Why:* no tearing, and a draw can't spin faster than the panel.
- **One renderer per window,** laid out in 640x480 units scaled to the panel.
- **Batch fills** (`SDL_RenderFillRects` for many rects in one call). *Why:* each call is a draw call; a list of rows
  drawn one call each adds up on the A55.
- **Keep the top panel static when nothing moves.** *Why:* it is the larger area to redraw, and a static page costs
  nothing between the 2 s refreshes.
- **Time things on the device, not the Mac.** *Why:* the handheld is roughly 5x slower: a graph measured at 0.85 ms a
  frame on a Mac took about 4 ms there. Log timings (`SDL_GetTicks` around the work) and read them from `log.txt`.

## Running Java (or any big helper process)

Some features are easiest as a server in another language. The media player runs a JVM server (a Java 21 runtime and a
server jar) as its manga engine. What was measured on the RG DS (981 MB RAM, no swap, 4x A55), and what it does:

- **Prefer OpenJ9 (IBM Semeru 21 JRE) over HotSpot.** The same server idles at about **290-300 MB on OpenJ9** against
  366-374 MB on HotSpot (about 411 MB resident with Temurin and the HotSpot flags below). *Why it matters:* on 1 GB with
  no swap that gap decided whether the kernel's OOM killer took the server in the middle of a chapter. Start takes about
  25-30 s; idle CPU is under 1%.
- **OpenJ9 flags:** `-Xmx256m` (a cap: the heap is not the only memory a JVM holds), `-Xms8m` (the heap starts small and
  grows as used), `-Xquickstart` (lighter early JIT: the server mostly waits on the network),
  `-Xshareclasses:name=myapp,cacheDir=<state>/classcache` (a shared class cache on the card: later starts are quicker
  and use less memory), `-Xtune:virtualized`, `-Djava.awt.headless=true`.
- **HotSpot as the second choice:** `-Xmx256m -XX:+UseSerialGC -XX:TieredStopAtLevel=1 -Djava.awt.headless=true`.
  *Why:* the serial GC has no GC threads to feed; C1 only skips the costly C2 compiles.
- **Headless only.** BananaOS has no X11 libraries: anything needing AWT, Chromium or CEF won't start.
- **The runtime is an optional download add-on**, not in the base zip. *Why:* the JRE is a 58 MB `.tar.gz` and a server
  jar can be 180 MB; most users never need it. **Pin each download by SHA-256** and check it before unpacking or
  running. *Why:* it is code you are about to execute.
- **Unpacking on the handheld:** `/data` is **exFAT, so no symlinks**: skip the JRE's `legal/` folder (symlinks) or the
  copy fails. **Don't name the runtime's folder `jre`:** some class scanners take a `java.home` ending in `/jre` to mean
  its parent. BusyBox `tar` has **no `z`** (decompress first, or pipe through `gzip -d`), BusyBox `wget` has **no
  HTTPS**, and there is **no `sftp-server`** (copy with `ssh host 'cat > file'` or tar over ssh). Download from your app
  instead (libcurl is on the system).
- **DNS:** the JVM uses the OS's resolver; DNS-over-HTTPS (where a network's DNS blocks a site) needs your own
  `InetAddressResolverProvider` jar on the classpath.
- **Start it as a child process, never blocking the main thread** (`posix_spawn`, in a process group of its own so one
  signal ends it and everything it started: `spawnChild` in `src/child.*`). **Talk to it over HTTP on 127.0.0.1**,
  bound to localhost only. Poll its readiness from a thread.
- **Show a waiting state with real progress** while it starts: seconds counted against the usual start time, a bar
  that stops short of full (an estimate must not say done), "taking longer than usual" past it, a way to cancel, and
  the action done by itself when it's ready. *Why:* the first version showed a passing note and nothing else for 28 s;
  it worked, and users took it as frozen. Warm it in advance when the user is likely to need it.
- **Stop it after idle** (5 minutes in the player). *Why:* it holds a third of the device's memory.
- **Never run it beside a video** (or heavy audio work): the 1 GB can't hold both. In the player a video wins and the
  engine is stopped; inside the reader the video is stopped instead.
- **Kill it on quit and on SIGTERM; write its pid to a file and end a leftover at the next start** (checking that
  `/proc/<pid>/cmdline` still names your folder, since pids are reused). *Why:* an orphan keeps 300-400 MB after your app
  has gone, and the next app gets OOM-killed. `stopChild` sends SIGTERM to the group at once and SIGKILL 3 s later
  from a thread, so a quit never waits.

## Building for aarch64 in Docker

- `packaging/build-bundle.sh` runs itself in an **arm64 Debian 12** container (`--platform linux/arm64`: native on
  Apple silicon, emulated elsewhere). Docker must be running.
- **It's cached:** the build tools are baked into an image tagged by a hash of its Dockerfile (built once, rebuilt only
  when the package list changes); the build folder (`.cache/build`) and ccache (`.cache/ccache`) are kept between runs;
  the commit goes into one generated file (`version.cpp`), so a new commit recompiles one file. A second run with
  nothing changed takes under a second; the first, about 40 s (most of it the image).
- `--quick`: the bundle only, no tests, no zip (for pushing to the handheld). `--clean`: the build from scratch.
- **A failed compile stops the script** with the errors. *Why:* never a bundle with the last good binary in it.
- **A quick arm64 compile check** of the Linux-only files (evdev, inotify), without the bundle:

  ```sh
  docker run --rm --platform linux/arm64 -v "$PWD":/src:ro debian:trixie bash -c "apt-get update -qq && \
    apt-get install -y -qq g++ cmake make pkg-config libsdl2-dev libsdl2-ttf-dev >/dev/null && \
    cmake -S /src -B /tmp/b -DCMAKE_CXX_FLAGS='-Wall -Wextra' && cmake --build /tmp/b -j && /tmp/b/app-test"
  ```

  *Why:* code under `#ifdef __linux__` never compiles on a Mac; glibc's headers also include less than the Mac's
  (`std::find` needs `<algorithm>` there).

## Crash logs

- A crash (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT) appends a backtrace to `<state>/crash.log`, then the process dies
  as it would have (exit status 139 or 134 in `launcher.txt`). The handler runs on its own stack (a stack overflow
  too) and makes only async-signal-safe calls.
- **Symbolicate:** the bundle's binary is stripped; `build-bundle.sh` keeps the unstripped one in
  `dist/symbols/my-app-<version>`. Keep it with each release. A frame reads `my-app(+0xOFFSET)`, which `addr2line`
  takes as it is:

  ```sh
  docker run --rm -v "$PWD/dist/symbols":/s:ro debian:bookworm bash -c "apt-get update -qq && \
    apt-get install -y -qq binutils >/dev/null && addr2line -Cfipe /s/my-app-0.1.0 0x3df8 0x47f4"
  ```

  The frames above `__kernel_rt_sigreturn` are the handler's; the one below it is where it crashed. The addresses are
  return addresses, so a line can be the one after the call. On a Mac: `atos -o build/my-app <address>`.

## Pushing to the handheld

- Put the handheld on your Wi-Fi with SSH enabled, and use its address with the user `root`.
- `packaging/build-bundle.sh --quick && tools/push-handheld.sh root@<handheld-ip>`. It:
  1. tries port 22 a few times (`nc -z -G 4 <handheld-ip> 22`). *Why:* the handheld's Wi-Fi naps to save power and the
     first connection can fail while it wakes;
  2. copies the installed folder on the handheld (fast, local) and sends only the changed files into the copy (rsync
     if both sides have it, else md5 sums compared and a tar stream over ssh). *Why:* Wi-Fi is slow and BananaOS may
     have no rsync;
  3. **quits the running app cleanly** (SIGTERM by its exact process name, up to 5 s; SDL turns SIGTERM into
     `SDL_QUIT`), then swaps the folders, keeping the old one as `MyApp.old`. *Why a clean quit:* a kill skips your
     undo (the lid sleep, the backlight, a child process). *Why the swap:* the running binary is never changed under
     it, and a push cut off halfway leaves the old folder whole.
- `APP_PORTS_DIR=/run/sd2/roms/ports` for an app on the second card.
- **Copying with a card reader on a Mac:** `COPYFILE_DISABLE=1 cp -X`, then `dot_clean` the folder. *Why:* macOS adds
  `._` files, and a stray `._My App.sh` shows up as a broken entry on the Ports shelf.
- Then read `<state>/log.txt` over SSH (`ssh root@<handheld-ip> cat /data/home/.config/myapp/log.txt`).

## Publishing to the BananaOS Store

The Store follows your GitHub releases: it reads the latest release's tag, downloads the asset, and installs the
package folder from it.

1. **Release:** bump `project(... VERSION x.y.z)` in `CMakeLists.txt`, commit, run `packaging/build-bundle.sh`, then
   `shasum -a 256 dist/MyApp-x.y.z-aarch64.zip > dist/MyApp-x.y.z-aarch64.zip.sha256` and
   `gh release create vx.y.z dist/MyApp-x.y.z-aarch64.zip dist/MyApp-x.y.z-aarch64.zip.sha256`. Versions compare
   numerically (0.1.10 > 0.1.9).
2. **The catalog entry** (`packaging/store/catalog-entry.json`): `release` names your repo, the tag prefix (`v`) and
   the asset (`MyApp-{version}-aarch64.zip`). `package.root` is the folder inside the zip, `check` a file that must be
   there, `executable` what gets the execute bit, `version_file` (`VERSION`, written by the bundle) and
   `version_match` how the installed version is read. `menu.launch` is `launch.sh`; `menu.media.image` is the tile's
   picture inside the package.
3. **The tile's picture needs a name of its own** (`share/myapp/myapp-tile.png`). *Why:* the Store copies every app's
   tile into one shared folder by file name alone, so an `icon.png` replaces another app's.
4. **Art for the catalog:** `icon.png` 32x32 and a `screen.png` 320x240, under `store/apps/<id>/` in the BananaLinux
   repo (a placeholder icon is in `packaging/store/`; take the screenshot on the device).
5. **Open a PR to JorreFog/BananaLinux** adding the entry to `store/catalog.json` and the two pictures. Say how the
   entry maps to the package, how it fits BananaOS (launcher, panels, touch, lid, state folder), what you bundle and
   what you take from the system, and what you tested on which device.

## Best practices

- **Never block the main thread.** No file listing of a large folder, network call, child-process wait or slow
  script on it. *Why:* one loop draws both panels and reads every button; a block freezes all of it.
- **Background threads post SDL user events and never touch the UI** (`SDL_PushEvent` is thread-safe; your drawing
  state isn't). *Why:* a race in drawing state shows up as a rare crash on the device only.
- **Redraw only when the drawn state changes** (a signature per panel). *Why:* CPU and battery; and add any new drawn
  state to the signature, or it won't appear.
- **Lay out in 640x480 logical units.** *Why:* one layout for both models.
- **Pin glibc's mmap threshold** (`mallopt(M_MMAP_THRESHOLD, 256 << 10)` at the top of `main`). *Why:* glibc raises the
  threshold to the largest block freed (up to 32 MB), after which big buffers come from the heap and freeing them gives
  nothing back; the media player measured 100-240 MB kept after a reader closed. **Judge memory on glibc** (Linux),
  never by the Mac's allocator, which behaves differently.
- **Default-initialise every struct member** (`int x = 0;`, `T t = {};`). *Why:* an uninitialised member read as 0 on a
  Mac and as stack garbage on Linux: a layout bug that only showed on the handheld.
- **Log the lines that would pin a handheld-only failure:** each input, each state change, each external call's
  result, timings. *Why:* you can't attach a debugger to a user's handheld; `log.txt` is all you get.
- **Pause on the lid and on SIGCONT;** resume only on the user's press.
- **No blocking network calls in destructors,** and don't destroy objects whose threads may be waiting on a server at
  quit (let the process end). *Why:* a quit that waits on a dead Wi-Fi connection looks like a hang, and BananaOS
  may then kill it before your undo runs.
- **A test budget:** on every change, build, run the pure tests (`app-test`) and one check aimed at the change; run the
  arm64 Docker check after touching Linux-only code; try it on the handheld before a release. *Why:* the bugs that
  matter show on the device; long desktop suites mostly don't catch them.

## Pitfalls (learned the hard way)

- **glibc vs the Mac's allocator:** memory freed on the Mac goes back; on glibc, without the pinned threshold, it may
  not. Measure on Linux.
- **Uninitialised structs fail only on Linux:** the Mac's fresh pages are often zeroed; glibc's stack is not.
- **glibc's headers include less:** code that compiles on the Mac fails on Linux for a missing `<algorithm>` or
  `<cstring>`. The arm64 Docker check catches it.
- **KMSDRM drops frames after start and wake:** a static page drawn once can stay black. Refresh for a while.
- **SDL's touch-to-mouse moves a cursor on KMSDRM** and can stall `SDL_PollEvent`; turn it off with your own reader.
- **A/B are swapped by SDL** (Xbox naming), X/Y are not.
- **F is shared with bananad:** act on F alone only on release with nothing between.
- **`run-port` darkens the bottom panel;** `run-store-app` doesn't. Test both ways in.
- **No fonts on the OS;** SDL_ttf has no fallback.
- **`/data` is exFAT:** no symlinks, no Unix permissions kept. Keep the execute bit in the
  Store's `executable` list.
- **BusyBox userland:** `ps -o pid,comm` (not `ps aux`), no `tar z`, no HTTPS `wget`, no `sftp-server`, `md5sum` not
  `md5`.
- **The Store copies tile pictures by file name:** use a unique one.
- **macOS's `._` files** become broken shelf entries: `COPYFILE_DISABLE=1`, `dot_clean`.
- **A bash launcher with a foreground child never sees TERM:** run it in the background and `wait`.
- **The speakers' amplifiers sometimes fail to start** (one speaker or none): a driver issue, not yours.
- **The handheld's Wi-Fi naps:** retry SSH.
- **1 GB, no swap:** the OOM killer is your crash reporter for memory; a helper process left running takes the next
  app down.
