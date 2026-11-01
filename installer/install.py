#!/usr/bin/env python3
"""Install the local native wallpaper without root or network access."""
import argparse
import datetime
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def install(payload):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='verify the package without installing')
    parser.add_argument('--no-start', action='store_true', help='install without starting or reloading the desktop')
    args = parser.parse_args()
    home = Path.home()
    config = Path(os.environ.get('XDG_CONFIG_HOME', home / '.config'))
    target = home / 'Tools/stillwater'
    link = home / '.local/bin/stillwater'
    native = payload / 'native'
    binary = native / 'build/stillwater-native'
    binary.chmod(0o755)
    try:
        probe = subprocess.run([str(binary), '--scene', 'pond', '--benchmark'], capture_output=True, text=True)
    except OSError:
        probe = None
    if probe is None or probe.returncode:
        needed = ('cc', 'pkg-config', 'wayland-scanner', 'strip')
        if not all(shutil.which(name) for name in needed):
            sys.exit('The bundled binary is incompatible. Install base-devel, cairo, wayland and wayland-protocols, then rerun this installer.')
        run('bash', str(native / 'build.sh'))
        probe = run(str(binary), '--scene', 'pond', '--benchmark', capture_output=True)
    print('Renderer check:', probe.stdout.strip())
    if args.check:
        print('Package OK. No desktop or installed files changed.')
        return
    autostart = config / 'hypr/autostart.lua'
    if autostart.is_file():
        line = 'o.launch_on_start(' + json.dumps(str(link) + ' start') + ')'
        comment = '--'
    else:
        autostart = config / 'hypr/autostart.conf'
        if not autostart.is_file():
            sys.exit('No supported Omarchy autostart.lua or autostart.conf found. Nothing was installed.')
        line = 'exec-once = ' + shlex.quote(str(link)) + ' start'
        comment = '#'
    if link.exists() or link.is_symlink():
        if not link.is_symlink() or link.resolve() != target / 'stillwater':
            sys.exit(f'{link} already belongs to another installation. Nothing was changed.')
    if not args.no_start:
        if not shutil.which('hyprctl') or not os.environ.get('WAYLAND_DISPLAY'):
            sys.exit('Run this installer in a terminal inside your Omarchy desktop, or use --no-start.')
        before = run('hyprctl', 'configerrors', capture_output=True).stdout.strip()
        if before:
            sys.exit('Hyprland already has configuration errors; fix them before installing:\n' + before)
    target.mkdir(parents=True, exist_ok=True)
    if (target / 'stillwater').is_file() and (target / 'stillwater-native').is_file():
        run(str(target / 'stillwater'), 'stop')
    for source, destination, mode in [(binary, target / 'stillwater-native', 0o755),
                                       (native / 'stillwater', target / 'stillwater', 0o755),
                                       (native / 'stillwater_tui.py', target / 'stillwater_tui.py', 0o644),
                                       (payload / 'README.md', target / 'README.md', 0o644)]:
        temporary = destination.with_suffix('.installing')
        shutil.copyfile(source, temporary)
        temporary.chmod(mode)
        temporary.replace(destination)
    link.parent.mkdir(parents=True, exist_ok=True)
    if not link.is_symlink():
        link.symlink_to(target / 'stillwater')
    settings_dir = config / 'stillwater'
    settings_dir.mkdir(parents=True, exist_ok=True)
    settings_path = settings_dir / 'settings.json'
    try:
        settings = json.loads(settings_path.read_text())
    except (FileNotFoundError, ValueError):
        settings = {}
    if not isinstance(settings, dict):
        settings = {}
    if settings.get('scene') not in ('pond', 'sky', 'mountains', 'forest', 'rain', 'dunes'):
        settings['scene'] = 'pond'
    if not isinstance(settings.get('fps'), int) or not 1 <= settings['fps'] <= 30:
        settings['fps'] = 12
    if settings.get('theme') not in ('midnight', 'paper', 'dawn', 'mist', 'ember', 'moonlight'):
        settings['theme'] = 'midnight'
    if type(settings.get('speed')) not in (float, int) or not .25 <= settings['speed'] <= 2:
        settings['speed'] = 1.0
    settings['enabled'] = True
    settings_path.write_text(json.dumps(settings) + '\n')
    apps = home / '.local/share/applications'
    apps.mkdir(parents=True, exist_ok=True)
    for slug, title in [('pond', 'Koi pond'), ('sky', 'Night sky'), ('mountains', 'Mountains'), ('forest', 'Firefly forest'), ('rain', 'Rain garden'), ('dunes', 'Quiet dunes'), ('stop', 'Restore background')]:
        (apps / f'stillwater-{slug}.desktop').write_text(
            '[Desktop Entry]\nType=Application\nName=Stillwater — ' + title +
            '\nComment=Native animated ASCII wallpaper\nExec="' + str(link).replace('"', '\\"') + '" ' + slug +
            '\nIcon=preferences-desktop-wallpaper\nTerminal=false\nStartupNotify=false\nCategories=Settings;DesktopSettings;\nKeywords=wallpaper;ASCII;ambient;stillwater;\n')
    (apps / 'stillwater-settings.desktop').write_text(
        '[Desktop Entry]\nType=Application\nName=Stillwater — Settings\nComment=Choose scenes, palettes and animation settings\nExec="' + str(link).replace('"', '\\"') + '" tui\nIcon=preferences-desktop-wallpaper\nTerminal=true\nCategories=Settings;DesktopSettings;\n')
    previous = autostart.read_text()
    backup = None
    if line not in previous:
        backup = autostart.with_name(autostart.name + '.before-stillwater-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
        shutil.copy2(autostart, backup)
        autostart.write_text(previous.rstrip() + '\n\n' + comment + ' Stillwater: native animated ASCII wallpaper.\n' + line + '\n')
    if not args.no_start:
        try:
            run('hyprctl', 'reload', capture_output=True)
            errors = run('hyprctl', 'configerrors', capture_output=True).stdout.strip()
            if errors:
                raise RuntimeError(errors)
        except (subprocess.CalledProcessError, RuntimeError) as error:
            if backup:
                shutil.copy2(backup, autostart)
                subprocess.run(['hyprctl', 'reload'], capture_output=True)
            settings['enabled'] = False
            settings_path.write_text(json.dumps(settings) + '\n')
            sys.exit('Hyprland reload failed; restored autostart and disabled the wallpaper.\n' + str(error))
        run(str(link), 'start')
    print('Installed. Find Stillwater in your launcher, or run: stillwater tui')
    print('Restore the original wallpaper: stillwater disable')
    if args.no_start:
        print('Desktop was not reloaded or started; run stillwater start in your Wayland session.')


if __name__ == '__main__':
    install(Path(__file__).resolve().parent)
