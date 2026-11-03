"""公開配布フォルダーとAPK・同梱ソースZIPの混入物を検査する。"""
import argparse
import hashlib
import io
import json
import pathlib
import re
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('folder', type=pathlib.Path)
args = parser.parse_args()
root = args.folder.resolve()
forbidden = {'.jks', '.keystore', '.p12', '.pfx', '.key', '.pem', '.nes', '.smc', '.sfc', '.srm', '.sav', '.wvs', '.state', '.pdb', '.ilk'}
secret = re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----|\bgithub_pat_[A-Za-z0-9_]{20,}|\bgh[pousr]_[A-Za-z0-9]{30,}|\bAKIA[A-Z0-9]{16}\b')
problems = []
count = 0

def inspect(name, data, depth=0):
    global count
    count += 1
    path = pathlib.PurePosixPath(name.replace('\\', '/'))
    if path.suffix.lower() in forbidden or path.name in {'release.properties', '.env', 'local.properties'} or 'signing' in path.parts:
        problems.append(name + ': excluded file type/path')
    if secret.search(data):
        problems.append(name + ': possible secret (value withheld)')
    if path.suffix.lower() in {'.zip', '.apk'}:
        if depth >= 4:
            raise RuntimeError('Unexpected archive nesting: ' + name)
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            assert archive.testzip() is None, name
            for entry in archive.infolist():
                if not entry.is_dir():
                    inspect(name + '!/' + entry.filename, archive.read(entry), depth + 1)

files = []
for file in sorted(root.rglob('*')):
    if file.is_file() and '.git' not in file.relative_to(root).parts:
        relative = file.relative_to(root).as_posix()
        inspect(relative, file.read_bytes())
        files.append(relative)

for required in ['README.md', 'THIRD_PARTY_NOTICES.md', 'Windows/licenses/snes9x/LICENSE', 'Windows/licenses/snes9x/source.zip', 'Windows/licenses/snes9x/wiizv_character_data.h', 'Windows/licenses/SDL3.txt']:
    assert required in files, required
apks = list((root / 'Android').glob('*.apk'))
assert len(apks) == 1
with zipfile.ZipFile(apks[0]) as apk:
    for required in ['assets/licenses/snes9x/LICENSE', 'assets/licenses/snes9x/source.zip', 'assets/licenses/NDK/NOTICE', 'assets/licenses/NDK/NOTICE.toolchain', 'assets/licenses/NDK/LLVM-NOTICE.txt']:
        assert required in apk.namelist(), required
    source_hash = hashlib.sha256(apk.read('assets/licenses/snes9x/source.zip')).hexdigest()
    assert source_hash == hashlib.sha256((root / 'Windows/licenses/snes9x/source.zip').read_bytes()).hexdigest()
    assert source_hash == '7f6dee7ab1a367da86bc86a87a204bf8fe0be389d62e7cfbd568dc2ccac32055'
for line in (root / 'SHA256SUMS.txt').read_text(encoding='utf-8-sig').splitlines():
    expected, name = line.split('  ', 1)
    assert hashlib.sha256((root / name).read_bytes()).hexdigest() == expected, name
print(json.dumps({'files': len(files), 'entries_with_archives': count, 'problems': problems}, ensure_ascii=False))
if problems:
    raise SystemExit(1)
print('PASS: secrets/ROM/save scan; nested archive integrity; license/source presence; checksums')
