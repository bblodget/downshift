#!/usr/bin/env bash
# Build a Release web version and zip it for itch.io (HTML project upload).
# Output: dist/<GAME>-web.zip, where GAME is the set(GAME ...) line in CMakeLists.txt
set -euo pipefail
cd "$(dirname "$0")"
NAME="$(sed -n 's/^set(GAME \(.*\))/\1/p' CMakeLists.txt)"

if ! command -v emcmake >/dev/null; then
    source ~/emsdk/emsdk_env.sh >/dev/null
fi

emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-web

rm -rf dist/web && mkdir -p dist/web
cp "build-web/${NAME}.js" "build-web/${NAME}.wasm" "build-web/${NAME}.data" dist/web/
cp "build-web/${NAME}.html" dist/web/index.html      # itch.io needs index.html at the zip root

# Licenses: OLC-3 clause 2 asks binary releases to reproduce the notices.
# The game's own license, plus PGE3's (found via the PGE3_DIR cmake used).
PGE3_DIR="$(sed -n 's/^PGE3_DIR:PATH=//p' build-web/CMakeCache.txt)"
cp LICENSE.md dist/web/LICENSE.md
cp "${PGE3_DIR}/LICENCE.md" dist/web/LICENSE-olcPixelGameEngine3.md
# The audio extension (OLC-3) keeps its notice in its header comment;
# include it only when the game actually uses the extension.
PGEX_AUDIO="third_party/olcPGEX3_Miniaudio.h"          # pinned copy, if present
[ -f "$PGEX_AUDIO" ] || PGEX_AUDIO="${PGE3_DIR}/extensions/miniaudio/olcPGEX3_Miniaudio.h"
if grep -q "olcPGEX3_Miniaudio.h" src/*.cpp && [ -f "$PGEX_AUDIO" ]; then
    sed -n '1,/^\*\//p' "$PGEX_AUDIO" > dist/web/LICENSE-olcPGEX3_Miniaudio.txt
fi
(cd dist/web && zip -q -r "../${NAME}-web.zip" .)
echo "wrote dist/${NAME}-web.zip ($(du -h "dist/${NAME}-web.zip" | cut -f1))"
echo "itch.io: Kind of project = HTML, upload the zip, tick 'This file will be played in the browser'."
echo "         Viewport 1280x720 (or tick fullscreen). Leave SharedArrayBuffer support unticked."
