import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path
from bte_protocol import crc16, parse_line, Framer
from bridge import Dedup

def frame(session=10, event_id=1, flags=1, version=1, reserved=0):
    payload = struct.pack('<BBBBIIQQ', version, 1, flags, reserved, session, event_id, 1234567, 1300000)
    return f'@BTE1,28,{payload.hex()},{crc16(payload):04X}'.encode()

class Tests(unittest.TestCase):
    def test_known_crc(self):
        self.assertEqual(crc16(b'123456789'), 0x4b37)

    def test_reference(self):
        path = Path(r'C:\Users\xiaoyang1024\Documents\Codex\2026-09-07\ble-1-sifli-sf32lb52-imu-lsm6ds3tr\outputs\board_rank1_20260913\parse_bte1.py')
        if not path.exists(): self.skipTest('local reference unavailable')
        spec = importlib.util.spec_from_file_location('reference', path)
        ref = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(ref)
        for sid, eid in [(0,0), (10,1), (0xffffffff,0xffffffff)]:
            original = ref.parse_event(frame(sid,eid))
            parsed = parse_line(frame(sid,eid))
            for key in ('session','event_id','beat_time_us','detected_time_us','flags'):
                self.assertEqual(parsed[key], original[key])

    def test_fragments_multiline_crlf_diagnostics(self):
        data = b'boot\r\nNSH: ' + frame() + b'\r\n' + frame(event_id=2) + b'\n'
        for step in range(1,len(data)+1):
            f=Framer(); lines=[]
            for i in range(0,len(data),step): lines.extend(f.feed(data[i:i+step]))
            parsed=[parse_line(l) for l in lines]
            self.assertEqual([v['event_id'] for v in parsed if v], [1,2])

    def test_bad_frames(self):
        for bad in [frame()[:-1]+b'Z',frame().replace(b',28,',b',27,'),frame(flags=2),frame(version=2),frame(reserved=1),frame()[:-4]+b'0000']:
            with self.assertRaises(ValueError): parse_line(bad)

    def test_ready(self):
        result=parse_line(b'@BTE1,READY,1,5,build_id=rank1_v28_bootfix_20260917\r\n')
        self.assertEqual(result['session'],5)
        self.assertIn('v28', result['build_id'])

    def test_bounded_framer(self):
        f=Framer(); self.assertEqual(f.feed(b'x'*10000),[])
        self.assertLessEqual(len(f.buffer),4096)
        self.assertEqual(f.feed(b'\n'+frame()+b'\n'),[frame()])
        self.assertEqual(f.counts['overlong_lines'],1)

    def test_persistent_dedup_session_gap(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'dedup.sqlite3'; d=Dedup(path)
            self.assertEqual(d.accept(parse_line(frame())),('accepted',0))
            self.assertEqual(d.accept(parse_line(frame())),('duplicate',0))
            self.assertEqual(d.accept(parse_line(frame(event_id=4))),('accepted',2))
            self.assertEqual(d.accept(parse_line(frame(event_id=2))),('out_of_order',0))
            self.assertEqual(d.accept(parse_line(frame(session=11))),('accepted',0))
            d.db.close(); d=Dedup(path)
            self.assertEqual(d.accept(parse_line(frame())),('duplicate',0))
            d.db.close()

if __name__ == '__main__': unittest.main(verbosity=2)
