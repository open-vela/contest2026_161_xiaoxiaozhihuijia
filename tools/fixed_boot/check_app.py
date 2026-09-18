#!/usr/bin/env python3
"""Check raw application ABI before programming only 0x12010000."""
import argparse, hashlib, json, struct
from pathlib import Path
BASE, LIMIT, RAM_BASE, STACK_LIMIT = 0x12010000, 0x129a0000, 0x20000000, 0x2007fc00

def check(data):
    if not 8 <= len(data) <= LIMIT-BASE:
        raise ValueError('App size is outside the fixed application partition')
    sp, reset = struct.unpack_from('<II', data)
    if not RAM_BASE < sp <= STACK_LIMIT or sp % 8:
        raise ValueError(f'Invalid initial SRAM stack: {sp:#x}')
    if not reset & 1 or not BASE <= (reset & ~1) < BASE+len(data):
        raise ValueError(f'Invalid application reset vector: {reset:#x}')
    return dict(bytes=len(data), sp=hex(sp), reset=hex(reset), sha256=hashlib.sha256(data).hexdigest())

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('image', type=Path)
    print(json.dumps(check(p.parse_args().image.read_bytes()), indent=2))
