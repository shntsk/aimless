# aimless

A tiny, extremely light **osu!-style fan game** written in C + SDL2.
**Silent aim is ON by default**: you only have to hit the timing, the game does not require you to be on the circle.
Loads **osu! stable skins** (hitcircle, overlay, approach circle, cursor, numbers, HUD digits, hit judgements, combo colours).

> ## ⚠️ Vibe-coded project
> This project was **written largely with AI assistance ("vibe coding")** and has not been audited.
> Expect bugs, rough edges and missing features. Use at your own risk, review the code, and PRs are welcome.

> ## Disclaimer
> aimless is an unofficial fan project. It is **not affiliated with or endorsed by ppy Pty Ltd or osu!**.
> It contains **no osu! code or assets**. The "osu!" name and branding belong to ppy; this project only uses it to say what it is inspired by. Skins are loaded from your own files; do not redistribute skins you don't have rights to.
> Silent aim is meant for **offline, casual play**. There is no online play, ranking, or score submission, and there never will be.

## Features
- Song select screen, **drag & drop `.osz` (songs), `.osk` (skins) or folders** onto the window to import
- Stable skin support: hitcircle, overlay, approach circle, cursor, numbers, HUD digits, hit judgements, `skin.ini` combo colours (`@2x` preferred, procedural fallback)
- **Hitsounds**: `normal/soft/drum` x `hitnormal/hitwhistle/hitfinish/hitclap` from the beatmap folder, then the skin, then generated sounds; sample sets and volume come from the timing points
- Soft silent aim by default (click within 2.5 circle radii still hits, cursor drawn where the mouse really is). `Tab` in the menu cycles soft / anywhere / off
- Clicks are judged at the frame time minus the delay SDL reports between queueing an event and handling it; the cursor is drawn at the newest mouse position (see *Latency notes*)
- Uncapped FPS by default; frame limiter, FPS counter and input delay readout in the HUD

## Song select
Organised like osu!lazer's song select, drawn in aimless' own Cupertino style: a fixed **info panel** on the left (cover, title, artist,
mapper, difficulty, CS/AR/OD/HP, length, BPM, object counts) that follows the selected set and difficulty, and on the right the carousel:
beatmaps are grouped into **sets**, the selected set expands to show its **difficulties** (ordered by star rating, with lazer's difficulty colours).
The **action bar** at the bottom has **Mods**, **Random** and **Options**, and a round **start button** sits in the bottom-right corner.

- **Search** by just typing: words match title, artist, mapper, difficulty name, source and tags.
  Filters work too: `stars>5`, `ar>=9.5`, `cs<4`, `od=8`, `hp<5`, `bpm>180`, `length<120` (seconds), e.g. `camellia stars>4`.
- **Sort**: artist, title, date added, difficulty, length, BPM. **Group**: none, artist, title, difficulty. Both are remembered.
- The **song preview** plays from the map's `PreviewTime` (needs an mp3/ogg), and the last played map is reselected on start.
- **Starting a map** (there is no Play button): click a difficulty once to select it; click the selected difficulty again, or double-click
  any difficulty, to start it. The round button in the bottom-right corner (or `Enter`) starts the difficulty that is selected at that moment.
- Keys: `Up`/`Down` sets, `Left`/`Right` difficulties, `Enter` start, `F1` mods, `F2` random, `F3` options, `F5` rescan, `Esc` clears the search / closes
  a panel / quits. The info panel also has previous / next difficulty buttons (same as `Left`/`Right`) and the search field has a clear button.
- **Mods** (`F1` or the Mods button): a sheet with the mods this build can really apply, **Easy** (CS/AR/OD/HP halved) and **Hard Rock**
  (CS x1.3, AR/OD/HP x1.4, capped at 10, and the playfield flipped vertically). They cannot be combined, are applied when the map is started
  and stay selected for the session (they are not saved to `settings.ini`). The info panel shows the adjusted CS/AR/OD/HP in the accent colour;
  the star rating is an estimate and is **not** recalculated for mods. Double Time, Half Time, Hidden, Flashlight and No Fail are not offered:
  the audio engine cannot change the song speed, and there is no health/fail system or Hidden/Flashlight rendering yet.
- Metadata shown comes straight from the `.osu` files: mapper, length, BPM and object count appear only when the beatmap has them
  (a set card shows the length and BPM of its first difficulty, or of the chosen one once the set is selected).
- Icons follow the SF Symbols look (round caps, one stroke weight) and are drawn in code by `src/icons.c`, so there are no image
  or font files to ship (the round start button is drawn in code too; the project ships no osu! branding) and they behave the same on Linux and Windows. Add a new icon to the enum in `src/icons.h` and to `build()` in `src/icons.c`.
- Scanning is cached in `~/.local/share/aimless/library.cache` (keyed by file path, size and mtime), so only new or changed maps are re-read.

**The star rating is an estimate.** It is a simplified strain model on the object heads (sliders and mods are not simulated), used for
ordering and colours only. It is not osu!'s official calculation and will not match the numbers you see in osu!.

## Settings
Open **Options** in the song select (or press `F3`): a side panel slides over the song select with the categories on the left
(Audio, Gameplay, Interface, Display, Skin; `Up`/`Down` or a click switch category) and the options of the selected category next to them.
`Esc`, the close button or a click outside the panel closes it. Everything is saved automatically to `~/.local/share/aimless/settings.ini`
(hand-editable). The UI is drawn in an iOS-style dark look; type in the song list to search, drop `.osz`/`.osk` to import.

| Group | Options |
|---|---|
| Audio | master, music and effect (hitsound) volume |
| Gameplay | **K1 / K2 keys** (default Z / X), background dim, background blur, show background, cursor size, cursor trail, hit lighting, playfield border (none / corners / full), silent aim (off / soft / anywhere, soft radius), input delay compensation (-100..100 ms) |
| Interface | show score, accuracy, combo, hit error meter, FPS counter, input delay |
| Display | frame limiter (VSync / 60 / 120 / 240 / 480 / unlimited), fullscreen |
| Skin | the skin used for the next map |

**Gameplay keys (K1 / K2)**: *Options -> Gameplay* has two rows, K1 (default `Z`) and K2 (default `X`). Click a row: it turns blue and says
"Press a key...", and the next key you press becomes the binding (it is saved to `settings.ini` as `key1` / `key2` right away).
`Esc`, a click anywhere else, closing the panel or switching window cancels and changes nothing. A key already used by the other row is
refused with a message (the capture stays open, press another key or `Esc`); `Esc`, the Windows/Super key, the lock keys, `PrintScreen`,
`Pause`, `Menu` and `Power` cannot be used. While a capture is open every key event belongs to it, so the key you press cannot click a
button, change the category or start a map, and the key (and its auto-repeat) is ignored until you release it. The mouse buttons always hit
as well. Missing, invalid or equal values in `settings.ini` fall back to `Z` / `X`.
Bindings are stored as **SDL scancodes** (the physical key position), not keycodes: on AZERTY / QWERTZ / Dvorak the default pair stays on the
same two keys under your left hand instead of following the letters, and the names on screen follow your layout (the key at the QWERTY `Z`
position shows as `W` on AZERTY). Gameplay compares the scancode of each key-down with two integers fixed when the map starts, so remapping
adds no work to the input path.

**Input delay**: with the option on, the HUD shows the lag SDL reports between queueing an input event and the frame that handled
it, plus your compensation. SDL2 stamps an event when it queues it (not when the OS received it), so this number cannot see time an
event spent waiting in the OS queue while a frame was still being drawn; it stays near 0 ms. For the real picture use `AIMLESS_PERF`
(below). Use the compensation setting for a constant offset.

### Compared with osu!lazer (osu!standard)
Lazer's global gameplay options (background dim/blur, hit lighting, cursor size, FPS counter) and its osu! ruleset options
(cursor trail, playfield border) have an equivalent here. Not here yet: snaking in/out sliders (needs slider support first),
cursor ripples, replay analysis options, key overlay, beatmap-specific offset, hitsound/skin source toggles.
Lazer's own list is the reference; this project copies none of its code or assets.

## Hitsounds (lazer-style lookup)
Per object: `hitSample` normal/addition set -> else timing point set -> else `SampleSet` of the map; volume and custom index the same way.
Files are looked up as `<set>-hit<normal|whistle|finish|clap>[N]` (N only when > 1, falls back to the unnumbered name), first in the beatmap folder, then the skin. A `hitSample` filename replaces the hitnormal. Slider nodes (head, repeats, tail) use their own edge sounds/sets; ticks play `<set>-slidertick`. Without sample files a synthetic beep is used.
The game clock is steered toward the music position (SDL_mixer >= 2.6) so hits and hitsounds line up with what you hear.

## Sliders
Bezier (with red anchors), linear, perfect-circle and catmull(as linear) curves; ticks, repeats and a tail judged by how much you followed (all = 300, half = 100, any = 50). Follow needs a held key/button and the cursor near the ball (silent aim widens that to your assist radius). Skin parts: `sliderb0..9`/`sliderb`, `sliderfollowcircle`, `reversearrow`, `sliderscorepoint`, `sliderendcircle(+overlay)`, `SliderBorder` / `SliderTrackOverride` in skin.ini, with fallbacks. The body is drawn like osu!lazer's legacy slider (soft shadow, `SliderBorder` band, gradient of the combo colour or `SliderTrackOverride` at 70 % alpha; constants taken from `LegacySliderBody.cs`, see `src/sliderbody.c`).
`combobreak.{wav,ogg,mp3}` from the skin plays when a combo of 20+ breaks (generated thud otherwise).

## Not implemented yet
Spinners (skipped), `sliderslide` loop sound, mods other than Easy and Hard Rock, results screen, Vulkan renderer.

## Install
Grab `aimless-*-x86_64-arch.tar.gz` from [Releases](../../releases) or build it yourself:
```sh
sudo pacman -S --needed base-devel sdl2 sdl2_image sdl2_mixer sdl2_ttf unzip fontconfig
make && sudo make install     # or: ./aimless
```
A `packaging/PKGBUILD` is included for `makepkg -si`.

## Use
```sh
aimless                              # song select
aimless import pack.osz MySkin.osk   # import from the terminal
aimless map.osu [skin]               # play a map directly; skin = folder, .osk file, or name in Skins/
```
Flags: `--no-silent`, `--assist=N` (radii; `0` = anywhere, `1` = normal), `--vsync`, `--songs=DIR`, `--skins=DIR`
(point those at your osu!stable `Songs`/`Skins` folders to use them without copying).
Data lives in `~/.local/share/aimless/{Songs,Skins}`. Keys: `K1`/`K2` (default `Z`/`X`, configurable in Options -> Gameplay) or the mouse buttons to hit, `Esc` back/quit.

## Skins
- **Import**: drop a `.osk` (or a skin folder) on the song-select window, or `aimless import MySkin.osk`. A `.osk` is extracted into `~/.local/share/aimless/Skins/<name>` through a hidden temp folder, so a broken archive installs nothing; the `.osk` is never modified, an already installed skin is never overwritten (the new one becomes `<name> (2)`), and an archive with a single wrapper folder is flattened. Archives are read by a built-in reader (stored and deflate, UTF-8 names); `unzip` / `bsdtar` (`tar.exe` on Windows) are only used as a fallback for exotic archives.
- **Lookup**: files are found ignoring case and with `/` or `\` separators, so Windows-made skins work on Linux. Images: `name@2x.png`, `name@2x.jpg`, `name.png`, `name.jpg`; sounds: `.wav`, `.ogg`, `.mp3`. A missing or undecodable file falls back per element; a file that exists but is fully transparent is kept transparent.
- **Circle sprites**: `hitcircle`, `hitcircleoverlay` and `approachcircle` each pick their own `@2x`/1x file and are drawn at their own logical size (pixels / 2 for `@2x`), scaled by the factor that maps the hitcircle to the circle diameter, so mixed 1x/2x skins and non-square or oversized sprites keep their proportions. Approach circles are drawn above all circles. `HitCircleOverlayAboveNumber` (also the common misspelling `...Numer`) sets whether the overlay is over the number (default yes).
- **skin.ini**: BOM, CRLF, indentation, `//` comments, `[Sections]` and `Key: Value` in any spacing are handled. Read today: `Combo1-8`, `SliderBorder`, `SliderTrackOverride`, `AllowSliderBallTint`, `Name`, `HitCirclePrefix` / `HitCircleOverlap` (circle numbers, below); `ScorePrefix`, `ComboPrefix` and their overlaps are parsed but not used yet.
- **Circle numbers**: digit `d` is the image `<HitCirclePrefix>-d` (default prefix `default`; sub-folders such as `fonts/hit/score` work, `@2x` preferred, case ignored). A digit whose file is missing falls back to `default-d`, then to a generated font digit. Each digit keeps its own width; `HitCircleOverlap` (logical px, may be negative) is how far the next digit is pulled back over the previous one. Image digits are scaled with the same factor as the circle sprites; generated digits keep the old fixed size.
- `AIMLESS_SKINLOG=1` prints every skin image that gets loaded, `AIMLESS_SFXLOG=1` every sample.
- `make test` runs the SDL-free tests (parser, lookup, import, mods, key-binding rules and their `settings.ini` round trip; the key-capture UI and the key handling in gameplay need SDL and are not part of it); `make test OSK=/path/skin.osk` adds a real skin.

## Releases
Push a tag like `v0.1.0` and the `release` workflow builds on Arch Linux and attaches `aimless-v0.1.0-x86_64-arch.tar.gz` (+ sha256) to a GitHub Release.

## Performance notes
Plain SDL2 renderer, no per-frame allocations, only the objects on screen are drawn, no shaders or post-processing.
If the FPS is low on an iGPU, run with `SDL_RENDER_DRIVER=opengl` or `SDL_RENDER_DRIVER=opengles2` and compare.

### Latency notes
Input is polled once per frame on the main thread, so a click waits in the OS queue until the current frame is finished
(drawing, `SDL_RenderPresent`, anything else the frame does). What the code does about it:
- the mouse position is read after the events were polled, a click is judged with the position it carried, and the cursor sprite
  uses the newest position at draw time;
- slider body textures are rasterised on the CPU (12-50 ms for a long slider at 720p-1440p). They are built a few rows at a time,
  about 3 ms per frame, for the sliders that are about to appear, instead of all at once in the frame where the slider shows up;
- the game clock follows the audio position with a time constant, not a per-frame gain, so the frame rate does not change it.

### Timing diagnostics (`AIMLESS_PERF`)
Set `AIMLESS_PERF=1` (Windows: `set AIMLESS_PERF=1` in the same console, then run `aimless.exe`) to get a summary when a map ends,
on stderr and appended to `perf.log` in the data folder. Nothing is printed during play. It reports the interval between input polls
(a click can wait that long before it is seen), time spent in `SDL_RenderPresent`, the SDL queue-to-frame lag of clicks, how many slider
bodies had to be built while visible, and the SDL version, video / audio driver, renderer (e.g. direct3d, opengl) and vsync state.
Please attach it when reporting lag.

## Layout
```
src/main.c      startup, flags
src/game.c      gameplay loop, input, judgement, rendering, HUD
src/ui.c        song select carousel + settings screen (immediate-mode, iOS style)
src/library.c   beatmap sets/difficulties, disk cache, sort/group, search filters
src/mapinfo.c   fast .osu metadata scan + estimated star rating
src/gfx.c       rounded rects, circles, cached text for the menus
src/paths.c     data dirs, import (.osz/.osk)
src/beatmap.c   .osu parser (metadata, timing points, hit objects)
src/skin.c      stable skin loader + fallbacks
src/skinfs.c    case-insensitive skin file lookup (no SDL)
src/skinini.c   skin.ini reader (no SDL)
tests/          SDL-free tests for the above (make test)
src/audio.c     hitsounds
src/settings.c  settings.ini load/save
src/keybind.c   K1/K2 validation, conflict rules, fallback to Z/X (no SDL)
src/font.c      TTF helpers + cached text
```

## Contributing
See [CONTRIBUTING.md](CONTRIBUTING.md). AI-assisted PRs are fine, but say so in the description.

## License
MIT (see [LICENSE](LICENSE)). Skins and beatmaps belong to their authors.
