# Stillwater

Six quiet, animated ASCII landscapes, light and dark palettes, and a terminal interface for making the desktop your own.

Stillwater runs as a small native Wayland wallpaper on Omarchy/Hyprland. The terminal interface uses Python’s standard library and adds no dependencies. Standalone HTML versions of the original three scenes work offline in a browser or a web wallpaper application.

![Copper koi in a dark ASCII pond](docs/previews/pond.png)

## Install on Omarchy

Download [stillwater-install.sh](https://github.com/GotinhaAzul/stillwater/raw/refs/heads/main/stillwater-install.sh), then run it in a terminal inside your Omarchy desktop:

```sh
bash ~/Downloads/stillwater-install.sh
```

The installer starts the koi pond, adds application launcher entries, and enables autostart. It needs no root access or downloads. It supports Omarchy's Lua and older `.conf` autostart files, backs up that file before editing, and leaves your original static wallpaper in place underneath.

The bundled native binary targets x86-64 Linux. The installer checks compatibility before changing your desktop and can rebuild from the included source when a compatible development toolchain is available.

To verify the package without installing:

```sh
bash ~/Downloads/stillwater-install.sh --check
```

Running the installer again updates an existing installation while preserving your selected scene and frame rate. See [installer details](installer/README.md).

## Switch scenes and settings

Run `stillwater` in a terminal, or open **Stillwater — Settings** in your application launcher:

```sh
stillwater tui
```

Use **Up/Down** to select a setting and **Left/Right** to change it. **Enter** saves and applies. **S** enables the wallpaper, **P** pauses/resumes, **X** restores your static wallpaper and disables animation at login, and **Q** closes the interface. Closing the TUI leaves the wallpaper running independently.

The interface controls the scene, theme, frame rate and animation speed. Settings stay pending until applied. If animation is disabled, applying settings saves them without starting a wallpaper. The interface handles small terminals and does not hold the controller lock while open.

| Landscape | What moves |
|---|---|
| `pond` — Koi pond | Copper koi and expanding water rings |
| `sky` — Night sky | Twinkling stars and passing meteors |
| `mountains` — Mountain lake | Slow mist and moonlit water |
| `forest` — Firefly forest | Fireflies among evergreen silhouettes |
| `rain` — Rain garden | Falling rain and ripples beside bamboo |
| `dunes` — Quiet dunes | Wind and drifting sand beneath a low sun |

![Firefly forest](docs/previews/forest.png)

| Theme | Appearance |
|---|---|
| `midnight` | Original dark colors |
| `paper` | Warm ivory, light |
| `dawn` | Soft peach, light |
| `mist` | Cool porcelain, light |
| `ember` | Dark copper |
| `moonlight` | Dark blue and silver |
| `sage` | Light botanical green |
| `sandstone` | Light warm sand and brown ink |
| `ocean` | Dark teal and sea glass |
| `plum` | Dark violet and lilac |

Each theme has a fixed light or dark appearance; choose a theme to change it. Every landscape has its own hand-selected material palette in each theme: copper koi and green pads, blue sky silhouettes, slate mountain layers and pine trees, green forest depths, blue rain and bamboo, and ochre dune ridges. Light appearances preserve these material colors and give foreground layers stronger ink. Every native scene supports every theme. Existing installations keep their scene and frame rate and default to midnight colors and normal speed.

![Custom light palettes for all six landscapes](docs/previews/light-landscapes.png)

![Rain garden in the mist palette](docs/previews/rain-mist.png)

For scripts and quick changes:

```sh
stillwater forest       # choose a scene and enable the wallpaper
stillwater theme paper  # set a palette; leaves a disabled wallpaper stopped
stillwater fps 8        # default 12; supported 1–30
stillwater speed 0.5    # default 1; supported 0.25–2
stillwater pause
stillwater status
stillwater status --json
stillwater list
stillwater disable      # static wallpaper, including after login
stillwater enable
```

`stillwater stop` stops the current animation without disabling the next login's autostart. `stillwater start` starts it again if enabled. With redirected input/output, bare `stillwater` prints status rather than entering the TUI. Palette, frame-rate and speed changes preserve a running wallpaper’s pause state.

## Lightweight by design

One low-priority native C process, with no browser, video decoder, network access, or continuously running Python process.

- Colored character tiles and static scene data are cached.
- Animation copies changed character pixels directly into shared-memory buffers.
- Only changed screen regions are reported to the compositor.
- Compositor frame callbacks pace output; pause blocks on events without polling.
- The wallpaper is click-through, sits below windows, and handles output hotplugging and integer display scaling.

Reference live measurements at 1360×768 and 12 fps were approximately **16 MB RAM** and **0.5–1% of one CPU core**. Paused and display-off samples used zero CPU ticks. Hardware, output count, resolution, scaling, and compositor behavior affect resource use; compositor work is separate from these process measurements.

The optimized renderer reduced 1080p drawing time by 63–79% against the first native implementation while preserving comparison images byte-for-byte. Details and limits are in [performance notes](docs/performance.md).

## Browser versions

Open [web/index.html](web/index.html) locally to explore all three scenes. `web/pond.html`, `web/sky.html`, and `web/mountains.html` open with controls hidden. Each file is self-contained and works offline.

Press **W** for controls, **Space** to pause, or **1–3** to switch scenes. The interface includes speed adjustment and PNG export. Reduced-motion preferences start the scene paused. Use a wallpaper application that supports local HTML for animated desktop use; an ordinary image wallpaper setting supports the exported PNG stills.

## Build from source

Requirements: a C compiler, `pkg-config`, Cairo development files, Wayland development files, `wayland-scanner`, `wayland-protocols`, Python 3, and standard Linux build tools. Tests also use the Wayland server library.

On Arch/Omarchy, the relevant packages are `base-devel`, `cairo`, `wayland`, `wayland-protocols`, and `python`. On Debian/Ubuntu: `build-essential`, `pkg-config`, `libcairo2-dev`, `libwayland-dev`, `wayland-protocols`, and `python3`.

```sh
make build
make test
# Optional AddressSanitizer + UndefinedBehaviorSanitizer checks:
SANITIZE=1 make test
# Rebuild the self-contained installer with your local binary:
make installer
```

Run directly inside a Wayland desktop supporting layer-shell:

```sh
./native/build/stillwater-native --scene dunes --theme paper --fps 12 --speed 0.5
```

Render a still or benchmark without a desktop:

```sh
./native/build/stillwater-native --scene mountains --render mountains.png
./native/build/stillwater-native --scene pond --benchmark
```

Tests cover cached rasterization and partial damage against complete Cairo redraws, alternating buffer reuse, 1×/2× scaling, real Wayland frame commits in an isolated test compositor, pause/resume, fresh/repeat installation in temporary homes, legacy settings migration, and keyboard interaction with the real TUI through pseudo-terminals. Renderer checks cover all 36 scene/palette combinations, three sizes and 1×/2× scaling. They do not connect to your desktop.

## Files and configuration

- Native source, controller and curses interface: `native/`
- Installer source and packager: `installer/`
- Offline HTML wallpapers: `web/`
- Installation: `~/Tools/stillwater/`, with `~/.local/bin/stillwater` on PATH
- Settings: `${XDG_CONFIG_HOME:-~/.config}/stillwater/settings.json`
- Logs: `${XDG_STATE_HOME:-~/.local/state}/stillwater/wallpaper.log`

The vendored [wlr-layer-shell protocol](native/wlr-layer-shell.xml) preserves its upstream copyright and license notice.
