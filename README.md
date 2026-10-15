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
- Uncapped FPS by default (`--vsync` to cap), FPS in the window title

## Not implemented yet
Sliders (head only, treated as circles), spinners (skipped), mods, results screen, Vulkan renderer.

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
aimless map.osu [skin_dir]           # play a map directly
```
Flags: `--no-silent`, `--assist=N` (radii; `0` = anywhere, `1` = normal), `--vsync`, `--songs=DIR`, `--skins=DIR`
(point those at your osu!stable `Songs`/`Skins` folders to use them without copying).
Data lives in `~/.local/share/aimless/{Songs,Skins}`. Keys: `Z`/`X`/mouse to hit, `Esc` back/quit.

## Releases
Push a tag like `v0.1.0` and the `release` workflow builds on Arch Linux and attaches `aimless-v0.1.0-x86_64-arch.tar.gz` (+ sha256) to a GitHub Release.

## Performance notes
Plain SDL2 renderer, no per-frame allocations, only the objects on screen are drawn, no shaders or post-processing.
If the FPS is low on an iGPU, run with `SDL_RENDER_DRIVER=opengl` or `SDL_RENDER_DRIVER=opengles2` and compare.

## Layout
```
src/main.c      startup, flags
src/game.c      gameplay loop, input, judgement, rendering, HUD
src/ui.c        song/skin select screen (SDL_ttf)
src/paths.c     data dirs, import (.osz/.osk), library scan
src/beatmap.c   .osu parser (metadata, timing points, hit objects)
src/skin.c      stable skin loader + fallbacks
src/audio.c     hitsounds
```

## Contributing
See [CONTRIBUTING.md](CONTRIBUTING.md). AI-assisted PRs are fine, but say so in the description.

## License
MIT (see [LICENSE](LICENSE)). Skins and beatmaps belong to their authors.
