#!/usr/bin/env python3
"""Build the fixed boot image/table. Neither input nor output depends on an app."""
from pathlib import Path
import hashlib, json, struct, subprocess

HERE = Path(__file__).resolve().parent
OUT = HERE / 'delivery'
APP_BASE = 0x12010000
APP_LIMIT = 0x129A0000
BOOT_FLASH = 0x12008000
BOOT_RAM = 0x20020000

def main():
    OUT.mkdir(exist_ok=True)
    subprocess.run(['arm-none-eabi-gcc', '-mcpu=cortex-m33', '-mthumb',
                    '-nostdlib', '-Wl,--build-id=none', '-T', str(HERE/'link.ld'),
                    str(HERE/'start.S'), '-o', str(OUT/'fixed_boot.elf')], check=True)
    subprocess.run(['arm-none-eabi-objcopy', '-O', 'binary',
                    str(OUT/'fixed_boot.elf'), str(OUT/'fixed_boot.bin')], check=True)
    boot = (OUT/'fixed_boot.bin').read_bytes()
    sp, reset = struct.unpack_from('<II', boot)
    assert sp == 0x20021000 and BOOT_RAM <= (reset & ~1) < BOOT_RAM + len(boot)
    # Fixed FTAB. BL is copied to RAM by ROM. HCPU is informational here;
    # this loader reads the app vectors directly, never its descriptor length.
    tab = bytearray(11280)
    struct.pack_into('<I', tab, 0, 0x53454346)
    entries = [(0,0,0,0)] * 16
    entries[0] = (0x12000000, 0x8000, 0, 0)
    entries[1] = (0x1200E000, 0x2000, 0, 0)
    for i in (3,7): entries[i] = (BOOT_FLASH, 0x6000, BOOT_RAM, 0)
    for i in (4,8): entries[i] = (APP_BASE, APP_LIMIT-APP_BASE, APP_BASE, 0)
    for i,e in enumerate(entries): struct.pack_into('<IIII', tab, 4+16*i, *e)
    for i in range(14):
        size = len(boot) if i in (1,5) else APP_LIMIT-APP_BASE if i in (2,6) else 0xffffffff
        struct.pack_into('<IHH', tab, 0x1000+512*i, size,
                         512 if size != 0xffffffff else 0,
                         2 if size != 0xffffffff else 0)
    struct.pack_into('<IIII', tab, 0x2c00, 0xffffffff, 0x12001200, 0x12001400, 0xffffffff)
    (OUT/'ftab_fixed.bin').write_bytes(tab)
    (OUT/'fixed_boot.disasm.txt').write_bytes(subprocess.check_output(
        ['arm-none-eabi-objdump','-d',str(OUT/'fixed_boot.elf')]))
    report = dict(boot_flash=hex(BOOT_FLASH), boot_ram=hex(BOOT_RAM),
                  boot_bytes=len(boot), app_base=hex(APP_BASE), app_limit=hex(APP_LIMIT),
                  app_max_bytes=APP_LIMIT-APP_BASE, board_validation='pending',
                  sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in OUT.glob('*.bin')})
    (OUT/'manifest.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))

if __name__ == '__main__': main()
