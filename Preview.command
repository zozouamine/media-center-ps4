#!/bin/sh
set -eu
preview_project=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd -- "$preview_project"
if [ ! -x build/host/ps4-media-center ]; then
    ./build.sh --host
fi
if [ ! -f tests/generated/extended-fixtures.json ]; then
    ./preflight.sh --host --tests
    python3 tests/generate_fixtures.py
fi
if [ "${1:-}" = "--video" ]; then
    exec build/host/ps4-media-center --source tests/generated/usb-demo \
        --data-dir build/live-preview-state --assets assets \
        --open 'Sample Video.avi' --script tools/preview-video-startup.tsv
fi
exec build/host/ps4-media-center --source tests/generated/usb-demo \
    --data-dir build/live-preview-state --assets assets \
    --script tools/preview-startup.tsv
