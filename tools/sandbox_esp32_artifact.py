#!/usr/bin/env python3
"""Store/verify an emulator-only snapshot. This does not establish provenance."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

REPO = Path(__file__).resolve().parent.parent
DEST = REPO / 'sandbox-artifacts' / 'esp32'
FILES = ('firmware.bin', 'bootloader.bin', 'partitions.bin', 'boot_app0.bin')


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def source_hashes(root):
    files = [Path('platformio.ini'), Path('tools/sandbox_esp32_build.sh'),
             Path('tools/sandbox_esp32_smoke.mjs')]
    present_dirs = []
    for directory in ('src', 'include', 'lib'):
        base = root / directory
        if base.is_symlink():
            raise ValueError(f'symlink not supported as build input: {base}')
        if not base.exists():
            continue
        if not base.is_dir():
            raise ValueError(f'not a directory: {base}')
        present_dirs.append(directory)
        for path in base.rglob('*'):
            if path.is_symlink():
                raise ValueError(f'symlink not supported as build input: {path}')
            if path.is_file():
                files.append(path.relative_to(root))
    return present_dirs, {p.as_posix(): digest(root / p) for p in sorted(files)}


def verify(images_dir):
    with (DEST / 'manifest.json').open(encoding='utf-8') as f:
        manifest = json.load(f)
    if manifest.get('schema') != 1:
        raise ValueError('unsupported artifact manifest schema')
    dirs, inputs = source_hashes(REPO)
    if manifest['source_directories'] != dirs or manifest['inputs_sha256'] != inputs:
        raise ValueError('source or build recipe changed; cached firmware is stale: rebuild it')
    if set(manifest['images_sha256']) != set(FILES):
        raise ValueError('invalid image list in manifest')
    for name in FILES:
        if digest(images_dir / name) != manifest['images_sha256'][name]:
            raise ValueError(f'artifact checksum mismatch: {name}')
    print('Cached image and current build inputs match the manifest.')
    print('Source commit:', manifest['source_commit'])
    print('WARNING: emulator-only; compiler provenance is unverified. Do not flash.')


def create(work):
    if subprocess.run(['git', '-C', str(REPO), 'status', '--porcelain', '--untracked-files=all', '--',
                       'platformio.ini', 'src', 'include', 'lib', 'tools/sandbox_esp32_build.sh',
                       'tools/sandbox_esp32_smoke.mjs'],
                      check=True, capture_output=True, text=True).stdout:
        raise ValueError('commit build inputs before creating an artifact snapshot')
    commit = subprocess.check_output(['git', '-C', str(REPO), 'rev-parse', 'HEAD'], text=True).strip()
    project = work / 'project'
    # The retained project's staged sources and recipe must still match Git.
    dirs, inputs = source_hashes(REPO)
    snapshot_dirs, snapshot = source_hashes(project)
    if snapshot_dirs != dirs or snapshot != inputs:
        raise ValueError('retained build inputs differ from current committed source')
    log = (work / 'emulator.log').read_text(encoding='utf-8')
    if 'SMOKE TEST PASSED: boot prompt and invalid-credential rejection' not in log:
        raise ValueError('retained build does not have a passing emulator smoke test')
    paths = {name: project / '.pio' / 'build' / 'esp32' / name for name in FILES[:3]}
    paths['boot_app0.bin'] = work / 'core/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'
    # Check all sources before replacing any existing snapshot.
    for path in paths.values():
        if not path.is_file():
            raise ValueError(f'missing build output: {path}')
    DEST.mkdir(parents=True, exist_ok=True)
    for name, path in paths.items():
        shutil.copyfile(path, DEST / name)
    manifest = {
        'schema': 1,
        'purpose': 'Emulator-only test image. Do not flash or release.',
        'source_commit': commit,
        'source_directories': dirs,
        'inputs_sha256': inputs,
        'images_sha256': {name: digest(DEST / name) for name in FILES},
        'compiler': 'Unverified third-party ivvil/rhodium commit 6cd22a00191d1bc7aa5ba9418c3655b9b0765577',
        'framework': 'espressif/arduino-esp32 commit 5e19e086c43d0fa5e5a596497ff8f11a0a43f6c2',
        'emulator_smoke': 'esp32-emu 1.2.0: boot prompt and short-password rejection',
    }
    (DEST / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    verify(DEST)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    verify_parser = sub.add_parser('verify')
    verify_parser.add_argument('--images-dir', type=Path, default=DEST)
    create_parser = sub.add_parser('create')
    create_parser.add_argument('retained_build', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'verify':
            verify(args.images_dir)
        else:
            create(args.retained_build)
    except (OSError, ValueError, KeyError, json.JSONDecodeError, subprocess.CalledProcessError) as error:
        print(f'artifact error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
