"""Drive CLI and curses via a private compositor and pseudo-terminal."""
from pathlib import Path
import fcntl
import json
import os
import pty
import select
import shutil
import signal
import struct
import subprocess
import tempfile
import termios
import time

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='stillwater-controller-test-') as directory:
    home = Path(directory)
    runtime = home / 'runtime'
    runtime.mkdir(mode=0o700)
    install = home / 'Tools/stillwater'
    install.mkdir(parents=True)
    for name in ('stillwater', 'stillwater_tui.py'):
        shutil.copy2(root / 'native' / name, install / name)
    shutil.copy2(root / 'native/build/stillwater-native', install / 'stillwater-native')
    command = str(install / 'stillwater')
    env = os.environ.copy()
    env.update(HOME=str(home), XDG_CONFIG_HOME=str(home / '.config'),
               XDG_STATE_HOME=str(home / '.local/state'), XDG_RUNTIME_DIR=str(runtime),
               WAYLAND_DISPLAY='stillwater-test', TERM='xterm-256color')

    def cli(*arguments, success=True):
        result = subprocess.run([command, *arguments], env=env, text=True, capture_output=True, timeout=3)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return result.stdout

    settings_path = home / '.config/stillwater/settings.json'
    cli('disable')
    cli('theme', 'paper', success=False)
    cli('speed', '.5')
    settings = json.loads(settings_path.read_text())
    assert 'theme' not in settings and settings['speed'] == .5 and not settings['enabled']
    for arguments in [('theme', 'unknown'), ('fps', '0'), ('fps', 'fast'), ('speed', 'nan'), ('speed', '3')]:
        cli(*arguments, success=False)
        assert json.loads(settings_path.read_text()) == settings
    cli('tui', success=False)  # A redirected pipe must not enter curses.
    server = subprocess.Popen([str(root / 'tests/build/wayland-fixture')], cwd=runtime,
                              env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    assert server.stdout.readline().strip() == 'READY'
    try:
        cli('forest')
        status = json.loads(cli('status', '--json'))
        assert status['process']['scene'] == 'forest' and 'theme' not in status['process']
        cli('pause')
        for scene in ('pond', 'sky', 'mountains', 'rain', 'dunes'):
            cli(scene)
            status = json.loads(cli('status', '--json'))
            assert status['settings']['scene'] == scene
            assert status['process']['scene'] == scene and status['process']['paused']
        cli('fps', '8')
        assert json.loads(cli('status', '--json'))['process']['fps'] == 8
        cli('disable')
        assert cli('status').strip() == 'Stopped'
    finally:
        cli('disable')
        server.terminate()
        _, errors = server.communicate(timeout=3)
        assert 'Assertion' not in errors, errors
    print('PASS: CLI validation, inactive setting changes, start, pause-preserving updates and disable on isolated Wayland.')

    # Old settings gain defaults; malformed fields recover without starting.
    settings_path.write_text(json.dumps({'scene': 'pond', 'theme': 'paper', 'fps': 12, 'enabled': False}))
    status = json.loads(cli('status', '--json'))
    assert 'theme' not in status['settings'] and status['settings']['speed'] == 1
    cli('fps', '12')
    assert 'theme' not in json.loads(settings_path.read_text())
    settings_path.write_text(json.dumps({'scene': 'bad', 'fps': 'bad', 'enabled': False}))
    assert json.loads(cli('status', '--json'))['settings']['scene'] == 'pond'
    settings_path.write_text(json.dumps({'scene': 'pond', 'fps': 12, 'enabled': False}))

    def open_tui(rows, columns):
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, columns, 0, 0))
        process = subprocess.Popen([command], stdin=slave, stdout=slave, stderr=slave,
                                   env=env, start_new_session=True)
        os.close(slave)
        return master, process

    def drain(master, seconds=.3):
        data = b''
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if select.select([master], [], [], .05)[0]:
                try:
                    data += os.read(master, 65536)
                except OSError:
                    break
        return data

    master, process = open_tui(28, 100)
    try:
        assert b'STILLWATER' in drain(master, .5)
        # Select sky, 15 fps and 1.25x, then apply while disabled.
        os.write(master, b'ljljl\n')
        drain(master)
        current = json.loads(settings_path.read_text())
        assert current == {'scene': 'sky', 'fps': 15, 'speed': 1.25, 'enabled': False}, current
        assert json.loads(cli('status', '--json'))['process'] == {}  # TUI holds no lock.
        os.write(master, b'p')
        assert b'stopped' in drain(master)
        os.write(master, b'q')
        process.wait(timeout=3)
        assert process.returncode == 0
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)
        os.close(master)
    master, process = open_tui(15, 40)
    try:
        assert b'Resize' in drain(master, .5)
        os.write(master, b'q')
        process.wait(timeout=3)
        assert process.returncode == 0
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)
        os.close(master)
    print('PASS: real TUI navigation, save/apply without activation, lock-free CLI access, error feedback, small-terminal fallback and clean exit.')
