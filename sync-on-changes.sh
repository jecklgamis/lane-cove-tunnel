#!/usr/bin/env bash
# Watch source and script files and rsync the whole directory to the remote on changes.

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
    rsync "${RSYNC_OPTS[@]}" "$DIR/" "$REMOTE" && \
        echo "$(date '+%Y-%m-%d %H:%M:%S') done" || \
        echo "$(date '+%Y-%m-%d %H:%M:%S') rsync failed"
}

echo "Syncing to ${REMOTE}"
do_sync

echo "Watching for changes in src/, config/, scripts/, Makefile, Dockerfile.peer..."
fswatch -ro \
    "$DIR/src" \
    "$DIR/config" \
    "$DIR/scripts" \
    "$DIR/Makefile" \
    "$DIR/Dockerfile.peer" \
| while read -r; do
    do_sync
done


