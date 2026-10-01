#!/usr/bin/env bash
# Fetch the CHOMPI TEMPO 1.0 factory card (MIT) at a pinned commit: 14
# chromatic samples, 14 slice samples, the boot buffer, presets.json and
# options.json (the firmware .bin is left out). Fetched from the upstream
# repository at a fixed commit rather than committed here.
set -euo pipefail

REPO="https://github.com/CHOMPI-Club/CHOMPI.git"
COMMIT="a73d732613da684e4de844619b690776f0f50ccf"
CARD_PATH="firmware/card-profiles/tempo-1.0"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
CACHE="$REPO_ROOT/.card-cache"
OUT="${1:-$REPO_ROOT/build/card}"

if [ ! -f "$CACHE/$COMMIT.ok" ]; then
    rm -rf "$CACHE/src"
    mkdir -p "$CACHE"
    git init -q "$CACHE/src"
    git -C "$CACHE/src" remote add origin "$REPO"
    git -C "$CACHE/src" config core.sparseCheckout true
    echo "$CARD_PATH/" > "$CACHE/src/.git/info/sparse-checkout"
    git -C "$CACHE/src" fetch -q --depth 1 origin "$COMMIT"
    git -C "$CACHE/src" checkout -q FETCH_HEAD
    touch "$CACHE/$COMMIT.ok"
fi

SRC="$CACHE/src/$CARD_PATH"
rm -rf "$OUT"
mkdir -p "$OUT"
n=0
for d in chromatic slice buffer; do
    mkdir -p "$OUT/$d"
    for f in "$SRC/$d"/*.wav; do
        cat "$f" > "$OUT/$d/$(basename "$f")"
        n=$((n + 1))
    done
done
cat "$SRC/presets.json" > "$OUT/presets.json"
cat "$SRC/options.json" > "$OUT/options.json"

if [ "$n" -ne 29 ]; then
    echo "fetch-card: expected 29 samples, got $n" >&2
    exit 1
fi
echo "fetch-card: $n samples -> $OUT"
