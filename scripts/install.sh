#!/bin/bash
# Copy a local build to the Move. Schwung's Tools menu picks it up.
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$(dirname "$SCRIPT_DIR")"
HOST="${MOVE_HOST:-ableton@move.local}"
DEST=/data/UserData/schwung/modules/tools/munchi-tempo

[ -d dist/munchi-tempo ] || { echo "Run ./scripts/build.sh first."; exit 1; }
ssh "$HOST" "mkdir -p $DEST"
# the card is large and rarely changes: copy it only when missing
if ssh "$HOST" "[ -f $DEST/card/chromatic/chroma_a1.wav ]"; then
    scp dist/munchi-tempo/module.json dist/munchi-tempo/help.json \
        dist/munchi-tempo/README.md dist/munchi-tempo/LICENSE dist/munchi-tempo/THIRD_PARTY.md dist/munchi-tempo/MANUAL.md "$HOST:$DEST/"
else
    scp -r dist/munchi-tempo/card dist/munchi-tempo/module.json dist/munchi-tempo/help.json \
        dist/munchi-tempo/README.md dist/munchi-tempo/LICENSE dist/munchi-tempo/THIRD_PARTY.md dist/munchi-tempo/MANUAL.md "$HOST:$DEST/"
fi
# upload beside, then rename: a running Munchi Tempo holds the old binary open
# ("text file busy" on a plain copy) and keeps it until it exits
scp dist/munchi-tempo/standalone "$HOST:$DEST/standalone.new"
scp scripts/boot-entry.sh "$HOST:$DEST/boot-entry.sh"
ssh "$HOST" "chmod +x $DEST/standalone.new $DEST/boot-entry.sh && mv -f $DEST/standalone.new $DEST/standalone"
echo "Installed to $DEST. Launch it from Schwung's Tools menu."
