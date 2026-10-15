# aimless

A tiny, extremely light **osu!-style fan game** written in C + SDL2.
**Silent aim is ON by default**: you only have to hit the timing, the game does not require you to be on the circle.
Loads **osu! stable skins** (hitcircle, overlay, approach circle, cursor, numbers, HUD digits, hit judgements, combo colours).

> ## ⚠️ Vibe-coded project
> This project was **written largely with AI assistance ("vibe coding")** and has not been audited.
> Expect bugs, rough edges and missing features. Use at your own risk, review the code, and PRs are welcome.

> ## Disclaimer
> aimless is an unofficial fan project. It is **not affiliated with or endorsed by ppy Pty Ltd or osu!**.
> It contains **no osu! code or assets**. Skins are loaded from your own files; do not redistribute skins you don't have rights to.
> Silent aim is meant for **offline, casual play**. There is no online play, ranking, or score submission, and there never will be.

## Features
- Circle gameplay with approach circles, AR/CS/OD from the `.osu` file, 300/100/50/miss
- Silent aim (default) or normal aiming with `--no-silent`
- Uncapped FPS by default (`--vsync` to cap), FPS shown in the window title
- Stable skin support (`@2x` preferred), `skin.ini` combo colours, procedural fallback if no skin is given

## Not implemented yet
Sliders (treated as circles on the head only), spinners (skipped), mods, song select, replays, storyboards, hitsounds.

## Build (Arch Linux)
```sh
sudo pacman -S --needed base-devel sdl2 sdl2_image sdl2_mixer
make
```

## Run
```sh
./aimless path/to/map.osu [path/to/skin] [--no-silent] [--assist=N] [--vsync]
```
`--assist=N` sets how far from a circle (in circle radii) a click still counts: default `2.5` (subtle correction), `0` = anywhere, `--no-silent` = `1` (no assist).
Keys: `Z` / `X` / mouse buttons to hit, `Esc` to quit.

Stable skins usually live in `.../osu!/Skins/<name>/` (for example `~/.local/share/osu-wine/osu!/Skins/` with osu-winello).
Audio: `.mp3`, `.ogg` and `.wav` work through SDL2_mixer.

## Performance notes
Plain SDL2 renderer, no per-frame allocations, only the objects on screen are drawn, no shaders or post-processing.
If the FPS is low on an iGPU, run with `SDL_RENDER_DRIVER=opengl` or `SDL_RENDER_DRIVER=opengles2` and compare.

## Layout
```
src/main.c      game loop, input, judgement, rendering, HUD
src/beatmap.c   .osu parser
src/skin.c      stable skin loader + fallbacks
```

## Contributing
See [CONTRIBUTING.md](CONTRIBUTING.md). AI-assisted PRs are fine, but say so in the description.

## License
MIT (see [LICENSE](LICENSE)). Skins and beatmaps belong to their authors.
