#!/usr/bin/env python3
"""Integrity/staleness checks for the deliberately cached emulator image."""
from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parent.parent


class CachedArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        for name in ('src', 'include', 'sandbox-artifacts'):
            shutil.copytree(REPO / name, self.root / name)
        if (REPO / 'lib').is_dir():
            shutil.copytree(REPO / 'lib', self.root / 'lib')
        (self.root / 'tools').mkdir()
        for name in ('sandbox_esp32_artifact.py', 'sandbox_esp32_build.sh', 'sandbox_esp32_smoke.mjs'):
            shutil.copy2(REPO / 'tools' / name, self.root / 'tools' / name)
        shutil.copy2(REPO / 'platformio.ini', self.root / 'platformio.ini')

    def tearDown(self):
        self.temp.cleanup()

    def verify(self):
        return subprocess.run([sys.executable, str(self.root / 'tools/sandbox_esp32_artifact.py'), 'verify'],
                              text=True, capture_output=True)

    def test_valid_snapshot(self):
        result = self.verify()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_source_change_rejected(self):
        with (self.root / 'src/main.cpp').open('a') as f:
            f.write('\n// changed\n')
        result = self.verify()
        self.assertIn('stale', result.stderr)
        self.assertNotEqual(result.returncode, 0)

    def test_new_source_file_rejected(self):
        (self.root / 'src/new.cpp').write_text('// new file\n')
        self.assertNotEqual(self.verify().returncode, 0)

    def test_recipe_change_rejected(self):
        with (self.root / 'tools/sandbox_esp32_build.sh').open('a') as f:
            f.write('\n# changed\n')
        self.assertNotEqual(self.verify().returncode, 0)

    def test_smoke_script_change_rejected(self):
        with (self.root / 'tools/sandbox_esp32_smoke.mjs').open('a') as f:
            f.write('\n// changed\n')
        result = self.verify()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('stale', result.stderr)

    def init_repo(self):
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        paths = ['platformio.ini', 'src', 'include', 'tools/sandbox_esp32_build.sh',
                 'tools/sandbox_esp32_smoke.mjs']
        if (self.root / 'lib').is_dir():
            paths.append('lib')
        subprocess.run(['git', '-C', str(self.root), 'add', *paths], check=True)
        subprocess.run(['git', '-C', str(self.root), '-c', 'user.name=Test',
                        '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'test'], check=True)

    def test_create_rejects_uncommitted_smoke_script(self):
        self.init_repo()
        with (self.root / 'tools/sandbox_esp32_smoke.mjs').open('a') as f:
            f.write('\n// uncommitted\n')
        result = subprocess.run([sys.executable, str(self.root / 'tools/sandbox_esp32_artifact.py'),
                                 'create', str(self.root / 'unused')], text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('commit build inputs', result.stderr)

    def staged_build(self):
        work = self.root / 'retained'
        project = work / 'project'
        project.mkdir(parents=True)
        shutil.copytree(self.root / 'src', project / 'src')
        shutil.copytree(self.root / 'include', project / 'include')
        if (self.root / 'lib').is_dir():
            shutil.copytree(self.root / 'lib', project / 'lib')
        shutil.copy2(self.root / 'platformio.ini', project / 'platformio.ini')
        (project / 'tools').mkdir()
        for name in ('sandbox_esp32_build.sh', 'sandbox_esp32_smoke.mjs'):
            shutil.copy2(self.root / 'tools' / name, project / 'tools' / name)
        return work

    def test_create_rejects_changed_staged_smoke_script(self):
        self.init_repo()
        work = self.staged_build()
        with (work / 'project/tools/sandbox_esp32_smoke.mjs').open('a') as f:
            f.write('\n// changed after build\n')
        result = subprocess.run([sys.executable, str(self.root / 'tools/sandbox_esp32_artifact.py'),
                                 'create', str(work)], text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('retained build inputs differ', result.stderr)

    def test_create_records_smoke_script(self):
        self.init_repo()
        work = self.staged_build()
        images = self.root / 'sandbox-artifacts/esp32'
        build = work / 'project/.pio/build/esp32'
        build.mkdir(parents=True)
        for name in ('firmware.bin', 'bootloader.bin', 'partitions.bin'):
            shutil.copy2(images / name, build / name)
        boot = work / 'core/packages/framework-arduinoespressif32/tools/partitions'
        boot.mkdir(parents=True)
        shutil.copy2(images / 'boot_app0.bin', boot / 'boot_app0.bin')
        (work / 'emulator.log').write_text('SMOKE TEST PASSED: boot prompt and invalid-credential rejection\n')
        result = subprocess.run([sys.executable, str(self.root / 'tools/sandbox_esp32_artifact.py'),
                                 'create', str(work)], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads((images / 'manifest.json').read_text())
        self.assertIn('tools/sandbox_esp32_smoke.mjs', manifest['inputs_sha256'])
        self.assertEqual(self.verify().returncode, 0)

    def test_image_corruption_rejected(self):
        image = self.root / 'sandbox-artifacts/esp32/firmware.bin'
        with image.open('r+b') as f:
            f.seek(32)
            b = f.read(1)
            f.seek(32)
            f.write(bytes([b[0] ^ 1]))
        result = self.verify()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('checksum mismatch', result.stderr)


if __name__ == '__main__':
    unittest.main()
