#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd -- "$project_dir"
build_mode=${1:---project}
build_jobs=${MC_BUILD_JOBS:-4}
host_build() {
    ./preflight.sh --host
    cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build/host -j "$build_jobs"
}
host_tests() {
    ./preflight.sh --host --tests
    host_build
    python3 tests/generate_fixtures.py
    ctest --test-dir build/host --output-on-failure
    python3 tests/verify_host.py --binary build/host/ps4-media-center
    python3 tests/verify_video.py --binary build/host/ps4-media-center
}
case "$build_mode" in
    --host) host_build ;;
    --test) host_tests ;;
    --sanitize)
        ./preflight.sh --host --tests
        python3 tests/generate_fixtures.py
        cmake -S . -B build/sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMC_SANITIZERS=ON
        cmake --build build/sanitize -j "$build_jobs"
        ctest --test-dir build/sanitize --output-on-failure
        ;;
    --ps4) ./preflight.sh --ps4; python3 tools/build_ps4.py --compile-only ;;
    --project|--all) host_tests; ./preflight.sh --ps4; python3 tools/build_ps4.py --compile-only ;;
    --pkg) ./preflight.sh --ps4; python3 tools/build_ps4.py --package ;;
    *) printf '%s\n' 'Usage: ./build.sh [--host|--test|--sanitize|--ps4|--project|--all|--pkg]' >&2; exit 2 ;;
esac
