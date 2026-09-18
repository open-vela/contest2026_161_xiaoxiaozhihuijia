"""BTE1 v1 / 28-byte payload. Matches the local board_rank1 parse_bte1.py.
This module never opens serial ports or transmits board commands.
"""
import re
import struct
from collections import Counter

EVENT = re.compile(rb'@BTE1,28,([0-9a-fA-F]{56}),([0-9a-fA-F]{4})$')
READY = re.compile(rb'@BTE1,READY,1,(\d+),build_id=([^\r\n,]+)(?:,.*)?$')

def crc16(payload):
    crc = 0xffff
    for value in payload:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0xa001 if crc & 1 else crc >> 1
    return crc

def parse_line(line):
    if isinstance(line, str):
        line = line.encode('ascii')
    line = line.rstrip(b'\r\n')
    pos = line.find(b'@BTE1,')
    if pos < 0:
        return None
    line = line[pos:]
    ready = READY.fullmatch(line)
    if ready:
        session = int(ready[1])
        if session > 0xffffffff:
            raise ValueError('READY session out of range')
        return dict(kind='ready', session=session, build_id=ready[2].decode('ascii'), raw=line.decode('ascii'))
    match = EVENT.fullmatch(line)
    if not match:
        raise ValueError('length_or_envelope')
    payload = bytes.fromhex(match[1].decode('ascii'))
    if crc16(payload) != int(match[2], 16):
        raise ValueError('crc')
    version, event, flags, reserved, session, event_id, beat, detected = struct.unpack('<BBBBIIQQ', payload)
    if version != 1 or event != 1 or reserved != 0 or flags & ~1:
        raise ValueError('unsupported_fields')
    return dict(kind='event', version=version, event=event, flags=flags,
                session=session, event_id=event_id, beat_time_us=beat,
                detected_time_us=detected, raw=line.decode('ascii'))

class Framer:
    """Bounded line framing; oversize lines are discarded through next newline."""
    def __init__(self, limit=4096):
        self.buffer = bytearray()
        self.limit = limit
        self.discarding = False
        self.counts = Counter()

    def feed(self, data):
        lines = []
        for value in data:
            if value == 10:
                if not self.discarding:
                    lines.append(bytes(self.buffer))
                self.buffer.clear()
                self.discarding = False
            elif not self.discarding:
                self.buffer.append(value)
                if len(self.buffer) > self.limit:
                    self.counts['overlong_lines'] += 1
                    self.buffer.clear()
                    self.discarding = True
        return lines
