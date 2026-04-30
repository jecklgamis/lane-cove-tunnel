#!/usr/bin/env bash
# Watch source and script files and rsync the whole directory to the remote on changes.
set -euo pipefail

REMOTE="jeck@nagasaki.local:~/workspace/lane-cove-tunnel"
DIR="$(cd "$(dirname "$0")" && pwd)"

RSYNC_OPTS=(
    --archive
    --delete
    --exclude='.git/'
    --exclude='.idea/'
    --exclude='peer'
)

do_sync() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') syncing..."
    rsync "${RSYNC_OPTS[@]}" "$DIR/" "$REMOTE"
    echo "$(date '+%Y-%m-%d %H:%M:%S') done"
}

do_sync

echo "Watching for changes in src/, config/, scripts, and Makefiles..."
fswatch -o \
    "$DIR/src" \
    "$DIR/config" \
    $(ls "$DIR"/*.sh "$DIR"/Makefile "$DIR"/Dockerfile* 2>/dev/null) \
| while read -r; do
    do_sync
done
