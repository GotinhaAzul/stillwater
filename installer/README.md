# Stillwater for Omarchy

Run the self-contained installer in a terminal on your Omarchy desktop:

```sh
bash ~/Downloads/stillwater-install.sh
```

It installs six native Wayland landscapes, six palettes (including three light palettes), and a dependency-free terminal interface; enables autostart; and starts the koi pond. It requires no root access or downloads. The package includes source and can rebuild itself if the bundled Linux binary is incompatible and the normal C/Wayland/Cairo development tools are installed.

You can inspect the installer first, or verify its payload without changing your desktop:

```sh
bash ~/Downloads/stillwater-install.sh --check
```

Open **Stillwater — Settings** from your application launcher, or run `stillwater tui`. You can also use:

```sh
stillwater pond
stillwater sky
stillwater mountains
stillwater forest
stillwater rain
stillwater dunes
stillwater theme paper
stillwater speed 0.5
stillwater pause
stillwater fps 8
stillwater disable     # restore your static background and disable autostart
stillwater enable
```

Default: 12 fps. One low-priority native process; no browser, video player or background Python process. Reference live measurements were about 16 MB RAM and 0.5–1% of one CPU core at 1360×768. Other computers and displays may differ.

Installation: `~/Tools/stillwater/`, with a symlink at `~/.local/bin/stillwater`. Settings: `~/.config/stillwater/settings.json`. Logs: `~/.local/state/stillwater/wallpaper.log`. The installer backs up the Hyprland autostart file before adding its launch command, and supports Omarchy's Lua and older .conf autostart files. The original theme and static wallpaper remain available beneath Stillwater.

The lighter renderer caches colored character tiles, precomputes scene constants, and reports only changed screen regions. Paired 1080p rendering benchmarks measured 63–79% less drawing time at the same 12 fps. All three scenes and the existing controls are included. Running this installer again updates an existing installation and preserves its selected scene and frame rate.

TUI controls: Up/Down selects a field, Left/Right changes it, Enter applies, S enables, P pauses/resumes, X restores static wallpaper and disables animation at login, and Q quits. Applying settings while animation is disabled leaves it stopped. Opening the interface creates no persistent background process.
