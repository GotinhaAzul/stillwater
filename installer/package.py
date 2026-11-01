from pathlib import Path
from io import BytesIO
import zipfile,base64,hashlib
root=Path(__file__).resolve().parent
native=root.parent/'native'
archive=BytesIO()
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
 for f in ['main.c','art.c','art.h','test-art.c','wlr-layer-shell.xml','build.sh','stillwater','stillwater_tui.py','build/stillwater-native']:
  z.write(native/f,'native/'+f)
 z.write(root/'install.py','install.py')
 z.write(root/'README.md','README.md')
data=archive.getvalue()
encoded=base64.encodebytes(data).decode()
script='''#!/usr/bin/env bash
# Stillwater native ASCII wallpaper installer. No sudo or network access.
# Run --check to verify without installing. Payload: ZIP + SHA-256.
set -euo pipefail
command -v python3 >/dev/null || { echo 'Python 3 is required.' >&2; exit 1; }
exec python3 - "$@" <<'STILLWATER_PYTHON'
import base64,hashlib,io,pathlib,runpy,tempfile,zipfile
payload=base64.b64decode("""PAYLOAD""")
if hashlib.sha256(payload).hexdigest() != 'DIGEST':
    raise SystemExit('Installer payload checksum failed.')
with tempfile.TemporaryDirectory(prefix='stillwater-install-') as staging:
    with zipfile.ZipFile(io.BytesIO(payload)) as archive:
        for entry in archive.infolist():
            path=pathlib.PurePosixPath(entry.filename)
            if path.is_absolute() or '..' in path.parts:
                raise SystemExit('Invalid archive path.')
        archive.extractall(staging)
    runpy.run_path(str(pathlib.Path(staging)/'install.py'),run_name='__main__')
STILLWATER_PYTHON
'''.replace('PAYLOAD',encoded).replace('DIGEST',hashlib.sha256(data).hexdigest())
(root.parent/'stillwater-install.sh').write_text(script)
print('Installer bytes:',len(script))
