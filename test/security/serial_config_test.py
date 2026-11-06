#!/usr/bin/env python3
"""Exercise the serial CLI against a POSIX pseudo-terminal, not real hardware."""
import json
import os
import pty
import select
import subprocess
import tempfile
import threading
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'tools' / 'serial_config.py'


class ClockSimulator:
    def __init__(self, fail_chunk=False):
        self.master, self.slave = pty.openpty()
        self.path = os.ttyname(self.slave)
        self.stop = threading.Event()
        self.fail_chunk = fail_chunk
        self.commands = []
        self.body = bytearray()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        pending = bytearray()
        while not self.stop.is_set():
            if not select.select([self.master], [], [], 0.1)[0]:
                continue
            try:
                data = os.read(self.master, 1024)
            except OSError:
                return
            if not data:
                return
            pending.extend(data)
            while b'\n' in pending:
                raw, _, remaining = pending.partition(b'\n')
                pending = bytearray(remaining)
                command = raw.rstrip(b'\r').decode('ascii', errors='replace')
                self.commands.append(command)
                if command == 'CONFIG PING':
                    reply = 'CONFIG PONG'
                elif command == 'CONFIG GET':
                    reply = 'CONFIG JSON {"count":60,"timezone":"UTC0"}'
                elif command.startswith('CONFIG BEGIN '):
                    self.body.clear()
                    reply = 'CONFIG READY'
                elif command.startswith('CONFIG CHUNK '):
                    _, _, offset, chunk = command.split(' ', 3)
                    if self.fail_chunk:
                        reply = 'CONFIG ERR invalid offset'
                    elif int(offset) != len(self.body):
                        reply = 'CONFIG ERR invalid offset'
                    else:
                        self.body.extend(chunk.encode('ascii'))
                        reply = 'CONFIG CHUNK OK'
                elif command == 'CONFIG COMMIT':
                    reply = 'CONFIG SAVED'
                else:
                    continue
                os.write(self.master, (reply + '\r\n').encode('ascii'))

    def close(self):
        self.stop.set()
        self.thread.join(timeout=2)
        os.close(self.master)
        os.close(self.slave)


class SerialConfigTest(unittest.TestCase):
    def run_cli(self, *args):
        return subprocess.run(['python3', str(SCRIPT), *args], text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)

    def test_get(self):
        clock = ClockSimulator()
        try:
            result = self.run_cli(clock.path, 'get')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout), {'count': 60, 'timezone': 'UTC0'})
            self.assertIn('CONFIG PING', clock.commands)
        finally:
            clock.close()

    def test_set_chunks_and_ascii_escapes(self):
        clock = ClockSimulator()
        config = {'timezone': 'Montréal', 'data': 'x' * 230}
        try:
            with tempfile.NamedTemporaryFile('w', encoding='utf-8') as file:
                json.dump(config, file, ensure_ascii=False)
                file.flush()
                result = self.run_cli(clock.path, 'set', file.name)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(clock.body), config)
            self.assertIn(b'\\u00e9', clock.body)
            self.assertGreater(sum(x.startswith('CONFIG CHUNK ') for x in clock.commands), 1)
            self.assertEqual(clock.commands[-1], 'CONFIG COMMIT')
        finally:
            clock.close()

    def test_error_does_not_commit(self):
        clock = ClockSimulator(fail_chunk=True)
        try:
            with tempfile.NamedTemporaryFile('w', encoding='utf-8') as file:
                json.dump({'count': 60}, file)
                file.flush()
                result = self.run_cli(clock.path, 'set', file.name)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('CONFIG ERR', result.stderr)
            self.assertNotIn('CONFIG COMMIT', clock.commands)
        finally:
            clock.close()

    def test_invalid_file_never_opens_port(self):
        with tempfile.NamedTemporaryFile('w', encoding='utf-8') as file:
            file.write('{invalid')
            file.flush()
            result = self.run_cli('/not-a-device', 'set', file.name)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Serial configuration failed', result.stderr)


if __name__ == '__main__':
    unittest.main()
