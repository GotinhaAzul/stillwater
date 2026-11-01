# Stillwater for Omarchy

Run the self-contained installer in a terminal on your Omarchy desktop:

```sh
bash ~/Downloads/stillwater-install.sh
```

It installs a small native Wayland wallpaper, enables autostart, and starts the koi pond. It requires no root access or downloads. The package includes source and can rebuild itself if the bundled Linux binary is incompatible and the normal C/Wayland/Cairo development tools are installed.

You can inspect the installer first, or verify its payload without changing your desktop:

```sh
bash ~/Downloads/stillwater-install.sh --check
```

Choose a scene from your application launcher by searching for Stillwater, or use:

```sh
stillwater pond
stillwater sky
stillwater mountains
stillwater pause
stillwater fps 8
stillwater disable     # restore your static background and disable autostart
stillwater enable
```

Default: 12 fps. One low-priority native process; no browser, video player or background Python process. Reference live measurements were about 16 MB RAM and 0.5–1% of one CPU core at 1360×768. Other computers and displays may differ.

Installation: `~/Tools/stillwater/`, with a symlink at `~/.local/bin/stillwater`. Settings: `~/.config/stillwater/settings.json`. Logs: `~/.local/state/stillwater/wallpaper.log`. The installer backs up the Hyprland autostart file before adding its launch command, and supports Omarchy's Lua and older .conf autostart files. The original theme and static wallpaper remain available beneath Stillwater.

The lighter renderer caches colored character tiles, precomputes scene constants, and reports only changed screen regions. Paired 1080p rendering benchmarks measured 63–79% less drawing time at the same 12 fps. All three scenes and the existing controls are included. Running this installer again updates an existing installation and preserves its selected scene and frame rate.
