"""Sign an already-built release (or benchmark) APK and preserve its exact native symbols.
Reads the local ignored keystore properties; never prints credentials.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(__doc__)
parser.add_argument('name')
parser.add_argument('--variant', choices=['release', 'benchmark'], default='release')
args = parser.parse_args()
assert re.fullmatch('[A-Za-z0-9_-]+', args.name)
propsfile = root / 'android/keystore.properties'
if not propsfile.exists():
    propsfile = Path('C:/Users/Occor/Documents/other_projects/goldeneye-vr/android/keystore.properties')
props = dict(line.strip().split('=', 1) for line in propsfile.read_text().splitlines()
             if '=' in line and not line.lstrip().startswith('#'))
key = Path(props['storeFile'])
if not key.is_absolute():
    relative = props['storeFile'].removeprefix('android/')
    key = propsfile.parent / relative
assert key.exists()
sdk = Path('C:/Users/Occor/AppData/Local/Android/Sdk')
java = Path('C:/Program Files/Java/jdk-20/bin/java.exe')
build = root / 'android/app/build'
outputs = build / 'outputs/apk' / args.variant
apk = outputs / (args.name + '.apk')
unsigned = outputs / f'app-{args.variant}-unsigned.apk'
if not unsigned.exists():
    unsigned = outputs / f'app-{args.variant}.apk'
shutil.copy2(unsigned, apk)
env = dict(os.environ, JAVA_HOME='C:/Program Files/Java/jdk-20',
           GEVR_KS_PASS=props['storePassword'], GEVR_KEY_PASS=props['keyPassword'])
subprocess.run([str(java), '-jar', str(sdk/'build-tools/36.0.0/lib/apksigner.jar'),
               'sign', '--ks', str(key), '--ks-key-alias', props['keyAlias'],
               '--ks-pass', 'env:GEVR_KS_PASS', '--key-pass', 'env:GEVR_KEY_PASS', str(apk)],
               env=env, check=True)
verify = subprocess.check_output([str(java), '-jar', str(sdk/'build-tools/36.0.0/lib/apksigner.jar'),
                                 'verify', '--verbose', '--print-certs', str(apk)], text=True)
assert '382ff89137e24be05a0eed4794ad8d6bd9e3b3011cca72f98a4885cdba875452' in verify
readelf = sdk/'ndk/25.1.8937393/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe'
def build_id(path):
    return re.search(r'Build ID: (\w+)', subprocess.check_output([str(readelf), '-n', str(path)], text=True))[1]
# Each variant has its own CMake output; take the unstripped library whose
# build ID matches the one packaged into this APK.
with zipfile.ZipFile(apk) as z:
    packaged = build / 'packaged-libgevr.so.tmp'
    packaged.write_bytes(z.read('lib/arm64-v8a/libgevr.so'))
identity = build_id(packaged)
packaged.unlink()
lib = next(path for path in (build/'intermediates/cxx/RelWithDebInfo').glob('*/obj/arm64-v8a/libgevr.so')
           if build_id(path) == identity)
dest = build/'outputs/symbols'/identity
dest.mkdir(parents=True, exist_ok=True)
shutil.copy2(lib, dest/'libgevr.so')
shutil.copy2(apk, dest/apk.name)
shutil.copy2(build/'outputs/native-debug-symbols'/args.variant/'native-debug-symbols.zip', dest/'native-debug-symbols.zip')
info = {'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        'dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=root, text=True).strip()),
        'variant': args.variant, 'apk': apk.name, 'sha256': hashlib.sha256(apk.read_bytes()).hexdigest(),
        'elfBuildId': identity, 'installed': False, 'published': False}
# Identical code gives the benchmark variant the release build ID; keep its
# metadata beside, not over, the release build.json.
meta = dest/'build.json' if args.variant == 'release' else dest/f'{apk.stem}.build.json'
meta.write_text(json.dumps(info, indent=2))
with zipfile.ZipFile(apk) as z:
    # The archive must correspond to the library actually packaged into this APK.
    packaged = dest/'packaged-libgevr.so'
    packaged.write_bytes(z.read('lib/arm64-v8a/libgevr.so'))
    assert identity in subprocess.check_output([str(readelf), '-n', str(packaged)], text=True)
    packaged.unlink()
print(json.dumps(info, indent=2))
