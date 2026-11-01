#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
mkdir -p tests/build
flags=(-O2 -std=c11 -Wall -Wextra)
if [[ ${SANITIZE:-0} == 1 ]]; then
  flags=(-g -O1 -std=c11 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
cc "${flags[@]}" native/art.c native/test-art.c -o tests/build/test-art $(pkg-config --cflags --libs cairo) -lm
ASAN_OPTIONS=detect_leaks=0 tests/build/test-art
wayland-scanner server-header native/wlr-layer-shell.xml tests/build/layer-shell-server.h
cc -O2 -Wall -Wextra -Itests/build tests/wayland-fixture.c native/build/layer-shell.c native/build/xdg-shell.c -o tests/build/wayland-fixture $(pkg-config --cflags --libs wayland-server)
python3 tests/check-wayland.py
bash -n stillwater-install.sh
bash stillwater-install.sh --check
python3 tests/check-installer.py
python3 tests/check-controller.py
