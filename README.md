# Downshift - A olcPixelGameEngine3 Game

Escape a 3D maze by changing gravity. OLC CodeJam 2026 Entry.

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
1280x720, leave SharedArrayBuffer unticked.

Native Linux: build Release, ship the binary with `assets/` beside it. Users
need libpng16, libX11, libXi, libGL (standard on desktop Linux).
