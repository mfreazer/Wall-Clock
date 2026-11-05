#!/usr/bin/env python3
"""Run the shipped serial client against real firmware linked to host stubs."""
import argparse
import json
import select
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--bridge', required=True)
parser.add_argument('--root', required=True)
args, unittest_args = parser.parse_known_args()
ROOT = Path(args.root).resolve()
BRIDGE = str(Path(args.bridge).resolve())


class SerialEndToEnd(unittest.TestCase):
    def setUp(self):
        self.bridge = subprocess.Popen([BRIDGE], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.addCleanup(self.stop_bridge)
        ready, _, _ = select.select([self.bridge.stdout], [], [], 5)
        self.assertTrue(ready, 'firmware PTY bridge did not become ready')
        self.device = self.bridge.stdout.readline().strip()
        self.assertTrue(self.device.startswith('/dev/'), 'firmware PTY bridge did not provide a device')
        self.assertIsNone(self.bridge.poll(), 'firmware PTY bridge exited at startup')
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def stop_bridge(self):
        if self.bridge.poll() is None:
            self.bridge.terminate()
        try:
            _, stderr = self.bridge.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            self.bridge.kill()
            _, stderr = self.bridge.communicate(timeout=5)
            self.fail('firmware PTY bridge failed to stop: ' + stderr)
        if self.bridge.returncode not in (-15, 0):
            self.fail(f'firmware PTY bridge exited {self.bridge.returncode}: {stderr}')

    def client(self, action, path=None, device=None):
        command = [sys.executable, str(ROOT / 'tools/serial_config.py'), device or self.device, action]
        if path is not None:
            command.append(str(path))
        result = subprocess.run(command, text=True, capture_output=True, timeout=30)
        self.assertIsNone(self.bridge.poll(), 'firmware PTY bridge exited while client was running')
        return result

    def get(self):
        result = self.client('get')
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def put(self, value):
        path = Path(self.tmp.name) / 'settings.json'
        path.write_text(json.dumps(value), encoding='utf-8')
        return self.client('set', path)

    def test_client_and_firmware_interoperate(self):
        initial = self.get()
        self.assertEqual(initial['count'], 60)
        self.assertEqual(initial['timezone'], 'UTC0')

        changed = dict(initial, count=72)
        result = self.put(changed)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Configuration saved.', result.stdout)
        self.assertEqual(self.get(), changed)

        rejected = dict(changed, count=999)
        result = self.put(rejected)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('CONFIG ERR invalid settings', result.stderr)
        self.assertEqual(self.get(), changed)

        changed['timezone'] = 'EST5EDT,M3.2.0/2,M11.1.0/2'
        result = self.put(changed)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.get(), changed)

        result = self.client('get', device=str(Path(self.tmp.name) / 'missing-device'))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Serial configuration failed', result.stderr)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *unittest_args])
