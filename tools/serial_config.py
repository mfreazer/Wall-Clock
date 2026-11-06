#!/usr/bin/env python3
"""Read or update clock settings over a local POSIX serial port (115200 baud)."""
import argparse
import json
import os
import select
import sys
import termios
import time
import tty

MAX_CONFIG = 2048


class SerialError(Exception):
    pass


class SerialPort:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            self.original = termios.tcgetattr(self.fd)
            tty.setraw(self.fd)
            settings = termios.tcgetattr(self.fd)
            settings[4] = settings[5] = termios.B115200
            settings[2] |= termios.CLOCAL | termios.CREAD
            termios.tcsetattr(self.fd, termios.TCSANOW, settings)
            termios.tcflush(self.fd, termios.TCIOFLUSH)
        except BaseException:
            os.close(self.fd)
            raise
        self.pending = bytearray()

    def close(self):
        try:
            termios.tcsetattr(self.fd, termios.TCSANOW, self.original)
        finally:
            os.close(self.fd)

    def send(self, command):
        data = command.encode('ascii') + b'\n'
        if len(data) > 161:
            raise SerialError('command exceeds firmware line limit')
        deadline = time.monotonic() + 10
        while data:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([], [self.fd], [], remaining)[1]:
                raise SerialError('serial write timed out')
            try:
                n = os.write(self.fd, data)
            except BlockingIOError:
                continue
            if n <= 0:
                raise SerialError('serial write failed')
            data = data[n:]

    def response(self, prefix, timeout=10):
        deadline = time.monotonic() + timeout
        while True:
            while b'\n' in self.pending:
                raw, _, tail = self.pending.partition(b'\n')
                self.pending = bytearray(tail)
                line = raw.rstrip(b'\r').decode('utf-8', errors='replace')
                if line.startswith('CONFIG ERR '):
                    raise SerialError(line)
                if (prefix == 'CONFIG JSON ' and line.startswith(prefix)) or line == prefix:
                    return line
            if len(self.pending) > 8192:
                raise SerialError('serial response too long')
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.fd], [], [], remaining)[0]:
                raise SerialError('serial response timed out')
            try:
                data = os.read(self.fd, 4096)
            except BlockingIOError:
                continue
            if not data:
                raise SerialError('serial port closed')
            self.pending.extend(data)

    def synchronize(self):
        # Opening a USB serial adapter can reset the board. Retry a harmless
        # ping while it boots instead of relying on a fixed sleep.
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            self.send('CONFIG PING')
            try:
                self.response('CONFIG PONG', timeout=1)
                return
            except SerialError as exc:
                if str(exc) != 'serial response timed out':
                    raise
        raise SerialError('clock did not respond to serial ping')


def read_config(port):
    port.send('CONFIG GET')
    line = port.response('CONFIG JSON ')
    return json.loads(line[len('CONFIG JSON '):])


def write_config(port, config):
    body = json.dumps(config, ensure_ascii=True, separators=(',', ':')).encode('ascii')
    if len(body) > MAX_CONFIG:
        raise SerialError('configuration exceeds 2048 bytes')
    port.send(f'CONFIG BEGIN {len(body)}')
    port.response('CONFIG READY')
    for pos in range(0, len(body), 80):
        chunk = body[pos:pos + 80].decode('ascii')
        port.send(f'CONFIG CHUNK {pos} {chunk}')
        port.response('CONFIG CHUNK OK')
    port.send('CONFIG COMMIT')
    port.response('CONFIG SAVED')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', help='serial device path, e.g. /dev/ttyUSB0')
    parser.add_argument('action', choices=('get', 'set'))
    parser.add_argument('file', nargs='?', help='JSON input file for set')
    args = parser.parse_args()
    if (args.action == 'set') != bool(args.file):
        parser.error('set requires a JSON file; get does not take one')
    try:
        config = None
        if args.action == 'set':
            with open(args.file, encoding='utf-8') as stream:
                config = json.load(stream)
        port = SerialPort(args.port)
        try:
            port.synchronize()
            if args.action == 'get':
                print(json.dumps(read_config(port), indent=2, ensure_ascii=False))
            else:
                write_config(port, config)
                print('Configuration saved.')
        finally:
            port.close()
    except (OSError, SerialError, ValueError) as exc:
        print(f'Serial configuration failed: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
