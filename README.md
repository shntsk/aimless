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
- Input judged at the real event timestamp, not at the next frame
- Uncapped FPS by default; frame limiter, FPS counter and input delay readout in the HUD

## Song select
Laid out like osu!lazer's carousel: beatmaps are grouped into **sets**, the selected set expands to show its **difficulties**
(ordered by star rating, with lazer's difficulty colours), and a details panel shows the cover, title, artist, mapper,
CS/AR/OD/HP, BPM, length and object counts.

- **Search** by just typing: words match title, artist, mapper, difficulty name, source and tags.
  Filters work too: `stars>5`, `ar>=9.5`, `cs<4`, `od=8`, `hp<5`, `bpm>180`, `length<120` (seconds), e.g. `camellia stars>4`.
- **Sort**: artist, title, date added, difficulty, length, BPM. **Group**: none, artist, title, difficulty. Both are remembered.
- The **song preview** plays from the map's `PreviewTime` (needs an mp3/ogg), and the last played map is reselected on start.
- Keys: `Up`/`Down` sets, `Left`/`Right` difficulties, `Enter` play, `F2` random, `F3` settings, `F5` rescan, `Esc` clears the search / quits.
  Click a set to select it, click the selected difficulty (or double-click a set) to play.
- Scanning is cached in `~/.local/share/aimless/library.cache` (keyed by file path, size and mtime), so only new or changed maps are re-read.

**The star rating is an estimate.** It is a simplified strain model on the object heads (sliders and mods are not simulated), used for
ordering and colours only. It is not osu!'s official calculation and will not match the numbers you see in osu!.

## Settings
Open **Settings** in the song select (or press `F3`). Everything is saved automatically to `~/.local/share/aimless/settings.ini`
(hand-editable). The UI is drawn in an iOS-style dark look; type in the song list to search, drop `.osz`/`.osk` to import.

| Group | Options |
|---|---|
| Audio | master, music and effect (hitsound) volume |
| Gameplay | background dim, background blur, show background, cursor size, cursor trail, hit lighting, playfield border (none / corners / full), silent aim (off / soft / anywhere, soft radius), input delay compensation (-100..100 ms) |
| Interface | show score, accuracy, combo, hit error meter, FPS counter, input delay |
| Display | frame limiter (VSync / 60 / 120 / 240 / 480 / unlimited), fullscreen |

**Input delay**: with the option on, the HUD shows the measured lag between the real input event and the frame that handled it,
plus your compensation. Hits are judged at the event timestamp, so frame rate does not add delay.

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
Bezier (with red anchors), linear, perfect-circle and catmull(as linear) curves; ticks, repeats and a tail judged by how much you followed (all = 300, half = 100, any = 50). Follow needs a held key/button and the cursor near the ball (silent aim widens that to your assist radius). Skin parts: `sliderb0..9`/`sliderb`, `sliderfollowcircle`, `reversearrow`, `sliderscorepoint`, `sliderendcircle(+overlay)`, `SliderBorder` / `SliderTrackOverride` in skin.ini, with fallbacks.
`combobreak.{wav,ogg,mp3}` from the skin plays when a combo of 20+ breaks (generated thud otherwise).

## Not implemented yet
Spinners (skipped), `sliderslide` loop sound, mods, results screen, Vulkan renderer.

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
Data lives in `~/.local/share/aimless/{Songs,Skins}`. Keys: `Z`/`X`/mouse to hit, `Esc` back/quit.

## Skins
- **Import**: drop a `.osk` (or a skin folder) on the song-select window, or `aimless import MySkin.osk`. A `.osk` is extracted into `~/.local/share/aimless/Skins/<name>` through a hidden temp folder, so a broken archive installs nothing; the `.osk` is never modified, an already installed skin is never overwritten (the new one becomes `<name> (2)`), and an archive with a single wrapper folder is flattened. Needs `unzip` (or `bsdtar`).
- **Lookup**: files are found ignoring case and with `/` or `\` separators, so Windows-made skins work on Linux. Images: `name@2x.png`, `name@2x.jpg`, `name.png`, `name.jpg`; sounds: `.wav`, `.ogg`, `.mp3`. A missing or undecodable file falls back per element; a file that exists but is fully transparent is kept transparent.
- **Circle sprites**: `hitcircle`, `hitcircleoverlay` and `approachcircle` each pick their own `@2x`/1x file and are drawn at their own logical size (pixels / 2 for `@2x`), scaled by the factor that maps the hitcircle to the circle diameter, so mixed 1x/2x skins and non-square or oversized sprites keep their proportions. Approach circles are drawn above all circles. `HitCircleOverlayAboveNumber` (also the common misspelling `...Numer`) sets whether the overlay is over the number (default yes).
- **skin.ini**: BOM, CRLF, indentation, `//` comments, `[Sections]` and `Key: Value` in any spacing are handled. Read today: `Combo1-8`, `SliderBorder`, `SliderTrackOverride`, `AllowSliderBallTint`, `Name`, `HitCirclePrefix` / `HitCircleOverlap` (circle numbers, below); `ScorePrefix`, `ComboPrefix` and their overlaps are parsed but not used yet.
- **Circle numbers**: digit `d` is the image `<HitCirclePrefix>-d` (default prefix `default`; sub-folders such as `fonts/hit/score` work, `@2x` preferred, case ignored). A digit whose file is missing falls back to `default-d`, then to a generated font digit. Each digit keeps its own width; `HitCircleOverlap` (logical px, may be negative) is how far the next digit is pulled back over the previous one. Image digits are scaled with the same factor as the circle sprites; generated digits keep the old fixed size.
- `AIMLESS_SKINLOG=1` prints every skin image that gets loaded, `AIMLESS_SFXLOG=1` every sample.
- `make test` runs the SDL-free tests (parser, lookup, import); `make test OSK=/path/skin.osk` adds a real skin.

## Releases
Push a tag like `v0.1.0` and the `release` workflow builds on Arch Linux and attaches `aimless-v0.1.0-x86_64-arch.tar.gz` (+ sha256) to a GitHub Release.

## Performance notes
Plain SDL2 renderer, no per-frame allocations, only the objects on screen are drawn, no shaders or post-processing.
If the FPS is low on an iGPU, run with `SDL_RENDER_DRIVER=opengl` or `SDL_RENDER_DRIVER=opengles2` and compare.

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
src/font.c      TTF helpers + cached text
```

## Contributing
See [CONTRIBUTING.md](CONTRIBUTING.md). AI-assisted PRs are fine, but say so in the description.

## License
MIT (see [LICENSE](LICENSE)). Skins and beatmaps belong to their authors.
