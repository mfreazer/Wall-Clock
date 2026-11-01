#!/usr/bin/env python3
"""Integrity/staleness checks for the deliberately cached emulator image."""
from pathlib import Path
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
        for name in ('sandbox_esp32_artifact.py', 'sandbox_esp32_build.sh'):
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
