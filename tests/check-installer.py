"""Exercise installation and upgrade in temporary homes, without desktop access."""
from pathlib import Path
import json
import os
import subprocess
import tempfile

installer = Path(__file__).resolve().parent.parent / 'stillwater-install.sh'
for extension in ('lua', 'conf'):
    with tempfile.TemporaryDirectory(prefix='stillwater-install-test-') as directory:
        home = Path(directory)
        config = home / '.config'
        (config / 'hypr').mkdir(parents=True)
        autostart = config / f'hypr/autostart.{extension}'
        autostart.write_text('-- existing\n' if extension == 'lua' else '# existing\n')
        runtime = home / 'runtime'
        runtime.mkdir(mode=0o700)
        env = os.environ.copy()
        env.update(HOME=str(home), XDG_CONFIG_HOME=str(config),
                   XDG_STATE_HOME=str(home / '.local/state'), XDG_RUNTIME_DIR=str(runtime))
        for attempt in range(2):
            if attempt:
                (config / 'stillwater/settings.json').write_text(json.dumps(
                    {'scene': 'mountains', 'theme': 'paper', 'fps': 8, 'speed': .5, 'enabled': False}))
            result = subprocess.run(['bash', str(installer), '--no-start'],
                                    env=env, capture_output=True, text=True)
            assert result.returncode == 0, result.stdout + result.stderr
        assert json.loads((config / 'stillwater/settings.json').read_text()) == {
            'scene': 'mountains', 'theme': 'paper', 'fps': 8, 'speed': .5, 'enabled': True}
        assert len(list((config / 'hypr').glob('*.before-stillwater-*'))) == 1
        assert autostart.read_text().count('Stillwater: native') == 1
        assert (home / '.local/bin/stillwater').is_symlink()
        assert len(list((home / '.local/share/applications').glob('stillwater-*.desktop'))) == 8
        result = subprocess.run([str(home / '.local/bin/stillwater'), 'status'],
                                env=env, capture_output=True, text=True)
        assert result.returncode == 0 and result.stdout.strip() == 'Stopped'
        assert (home / 'Tools/stillwater/stillwater_tui.py').is_file()
        print(f'PASS: {extension} install/upgrade preserves scene/rate; one backup and autostart entry.')
