# PGE3 jam template

Copy this directory (or use it as a GitHub template repo), change the
`set(GAME jamgame)` line at the top of `CMakeLists.txt`, `git init`, and
start in `src/main.cpp`. The binary, web files, and zip all take that name.

## Build and run

```bash
cmake -S . -B build          # once; picks g++-13 automatically on this machine
cmake --build build          # or :make in nvim
cd build && ./jamgame        # run from build/ so ./assets resolves
```

Options (pass to the configure step):

- `-DPGE3_DIR=/path/to/olcPixelGameEngine3` if the repo isn't at `../repo/olcPixelGameEngine3`
- `-DUSE_WAYLAND=ON` native Wayland instead of X11 (needs libdecor-0-dev, wayland-protocols >= 1.43)
- `-DUSE_STB=ON` load images via stb_image (needs `stb_image*.h` in the PGE3 root)
- `-DCMAKE_BUILD_TYPE=Release` for the jam submission (default is RelWithDebInfo)

## What's in main.cpp

- `main()` builds a `PGEConfig`: 256x240 logical pixels at 4x, vsync on.
- `Game::OnUserUpdate` handles Escape (quit), F1 (debug overlay), and the
  unfocused-window pause, then runs the current mode.
- Modes (`Title`, `Play`, `Pause`) are structs with `OnEnter/OnUpdate/OnExit`.
  `OnUpdate` returns the next mode. Replace them with your game.
- The debug overlay shows FPS, frame time, and GPU task/transfer counts.
  "up"/"down" transfers should be zero in steady state; if not, something is
  pulling an image back to the CPU each frame (usually `draw.Pixel`/`GetPixel`).

## Editor

`.nvim.lua` sets `makeprg`; `compile_commands.json` is symlinked from `build/`
for clangd. Both are gitignored.

## Web build (Emscripten)

```bash
source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web && cmake --build build-web
cd build-web && python3 -m http.server 8000     # open http://localhost:8000/jamgame.html
```

Assets are embedded via `--preload-file`, so nothing to copy. Debug builds
are ~6 MB of wasm; Release is ~0.6 MB.

## Packaging for itch.io

Web (recommended, plays in the browser): `./package-web.sh` builds Release
and writes `dist/jamgame-web.zip` with `index.html` at the root. On itch:
project kind HTML, upload the zip, tick "played in the browser", viewport
1024x960, leave SharedArrayBuffer unticked.

Native Linux: build Release, ship the binary with `assets/` beside it. Users
need libpng16, libX11, libXi, libGL (standard on desktop Linux).
