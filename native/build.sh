#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
mkdir -p build
wayland-scanner client-header wlr-layer-shell.xml build/layer-shell.h
wayland-scanner private-code wlr-layer-shell.xml build/layer-shell.c
wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml build/xdg-shell.c
cc -O3 -flto -DNDEBUG -std=c11 -Wall -Wextra -Wpedantic -Ibuild main.c art.c build/layer-shell.c build/xdg-shell.c -o build/stillwater-native $(pkg-config --cflags --libs cairo wayland-client) -lm
strip build/stillwater-native
