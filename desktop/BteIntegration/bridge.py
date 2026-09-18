"""Single-reader v28 serial -> localhost JSONL bridge. No firmware operations.
Ctrl+C closes host handles only. Events are never replayed on TCP reconnect.
"""
import argparse
import ctypes
import json
import re
import socket
import sqlite3
import sys
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path
from collections import Counter
from bte_protocol import Framer, parse_line

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'vendor'))

def ticks():
    value = ctypes.c_longlong()
    ctypes.windll.kernel32.QueryPerformanceCounter(ctypes.byref(value))
    return value.value

def frequency():
    value = ctypes.c_longlong()
    ctypes.windll.kernel32.QueryPerformanceFrequency(ctypes.byref(value))
    return value.value

class Evidence:
    def __init__(self, source):
        self.path = ROOT / 'evidence' / (datetime.now().strftime('%Y%m%d_%H%M%S_') + uuid.uuid4().hex[:6] + '_' + source)
        self.path.mkdir(parents=True)
        self.raw = (self.path / 'serial.bin').open('wb')
        self.log = (self.path / 'bridge.jsonl').open('w', encoding='utf-8', buffering=1)
        self.source = source

    def write(self, stage, **fields):
        self.log.write(json.dumps(dict(stage=stage, source=self.source, utc=datetime.now(timezone.utc).isoformat(), **fields), ensure_ascii=False) + '\n')

class Dedup:
    """Persistent accepted identities: bridge restart cannot replay an old event."""
    def __init__(self, path):
        self.db = sqlite3.connect(path)
        self.db.execute('CREATE TABLE IF NOT EXISTS events(session INTEGER, id INTEGER, raw TEXT, PRIMARY KEY(session,id))')
        self.db.execute('CREATE TABLE IF NOT EXISTS sessions(session INTEGER PRIMARY KEY, high INTEGER)')

    def accept(self, event):
        sid, eid = event['session'], event['event_id']
        old = self.db.execute('SELECT raw FROM events WHERE session=? AND id=?', (sid,eid)).fetchone()
        if old:
            return ('duplicate' if old[0] == event['raw'] else 'identity_collision'), 0
        row = self.db.execute('SELECT high FROM sessions WHERE session=?', (sid,)).fetchone()
        if row and eid <= row[0]:
            return 'out_of_order', 0
        gap = max(0, eid-row[0]-1) if row else 0
        with self.db:
            self.db.execute('INSERT INTO events VALUES(?,?,?)', (sid,eid,event['raw']))
            self.db.execute('INSERT OR REPLACE INTO sessions VALUES(?,?)', (sid,eid))
        return 'accepted', gap

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', default='COM7')
    ap.add_argument('--baud', type=int, default=1000000)
    ap.add_argument('--tcp-port', type=int, default=9877)
    ap.add_argument('--duration', type=float, default=0, help='0: until Ctrl+C')
    ap.add_argument('--start-app', action='store_true', help='Send approved command ONCE, only after observing NSH prompt and no READY/event')
    ap.add_argument('--observed-nsh-evidence', type=Path, help='Recent saved serial evidence ending in NSH prompt; permits one startup after reopening host port')
    args = ap.parse_args()
    import serial
    evidence = Evidence('board_serial')
    counts = Counter()
    dedup = Dedup(ROOT / 'board_serial_dedup.sqlite3')
    evidence.write('run', args={k:str(v) for k,v in vars(args).items()}, version='bte-desktop-1', qpc_frequency=frequency(), python=sys.version)
    print('Evidence:', evidence.path, flush=True)
    port = serial.Serial(port=None, baudrate=args.baud, bytesize=8, parity='N', stopbits=1,
                         timeout=0.05, write_timeout=1, xonxoff=False, rtscts=False, dsrdtr=False)
    # Set inactive levels BEFORE open. No reset pulses, flushes or control commands.
    port.dtr = False
    port.rts = False
    port.port = args.port
    sock = None
    framer = Framer()
    ack_framer = Framer()
    status = 'not_connected'
    current_session = None
    build_id = ''
    ready_raw = ''
    seen_board = False
    sent_start = False
    prompt_tail = b''
    last_connect = last_status = 0.0
    started = time.monotonic()
    stop_file = evidence.path / 'STOP'

    def send(message):
        nonlocal sock
        if sock is None:
            return False
        message.update(schema='bte_bridge_v1', source='board_serial', run_id=evidence.path.name,
                       qpc_frequency=frequency(), forward_ticks=ticks())
        try:
            sock.sendall((json.dumps(message, separators=(',', ':'))+'\n').encode('utf-8'))
            evidence.write('forwarded', message=message)
            return True
        except (OSError, socket.timeout) as exc:
            evidence.write('tcp_disconnected', error=str(exc))
            sock.close()
            sock = None
            return False

    try:
        port.open()
        status = 'serial_open'
        evidence.write('serial_open', port=args.port, baud=args.baud, dtr=False, rts=False)
        print('Serial open; no reset. Waiting for READY / BTE1.', flush=True)
        if args.observed_nsh_evidence:
            proof = args.observed_nsh_evidence
            tail = re.sub(rb'\x1b\[[0-9;]*[A-Za-z]', b'', proof.read_bytes()[-512:]).rstrip()
            if not args.start_app or not tail.endswith(b'nsh>') or time.time()-proof.stat().st_mtime > 300:
                raise ValueError('startup requires recent captured NSH prompt and --start-app')
            command = b'openvela_ble_probe --ui-paused --diag-off -c\n'
            port.write(command)
            sent_start = True
            evidence.write('serial_write', command=command.decode('ascii'), observed_nsh_evidence=str(proof))
        while not stop_file.exists() and (not args.duration or time.monotonic()-started < args.duration):
            now = time.monotonic()
            if sock is None and now-last_connect >= 1:
                last_connect = now
                try:
                    sock = socket.create_connection(('127.0.0.1', args.tcp_port), timeout=.15)
                    sock.settimeout(.02)
                    ack_framer = Framer()
                    evidence.write('tcp_connected')
                    last_status = 0
                except OSError:
                    sock = None
            if now-last_status >= 1:
                last_status = now
                send(dict(kind='status', state=status, session=current_session or 0,
                          build_id=build_id, raw=ready_raw, counts=dict(counts)))
            if sock is not None:
                try:
                    received = sock.recv(8192)
                    if not received:
                        sock.close()
                        sock = None
                        evidence.write('tcp_disconnected', error='eof')
                    else:
                        for line in ack_framer.feed(received):
                            evidence.write('application_ack', raw=line.decode('utf-8', 'replace'))
                except socket.timeout:
                    pass
                except OSError as exc:
                    evidence.write('tcp_disconnected', error=str(exc))
                    sock.close()
                    sock = None
            data = port.read(min(max(port.in_waiting,1),65536))
            rx = ticks()
            if not data:
                continue
            offset = evidence.raw.tell()
            evidence.raw.write(data)
            evidence.raw.flush()
            evidence.write('serial_chunk', host_rx_ticks=rx, offset=offset, length=len(data))
            prompt_tail = (prompt_tail + data)[-512:]
            for line in framer.feed(data):
                counts['lines'] += 1
                try:
                    event = parse_line(line)
                except (ValueError, UnicodeError) as exc:
                    counts['invalid'] += 1
                    evidence.write('rejected', reason=str(exc), raw=line.decode('ascii','replace'), host_rx_ticks=rx)
                    continue
                if event is None:
                    continue
                seen_board = True
                event['host_rx_ticks'] = rx
                evidence.write('parsed', message=event)
                if current_session != event['session']:
                    evidence.write('session_change', previous=current_session, current=event['session'])
                    current_session = event['session']
                    build_id = ''
                    ready_raw = ''
                if event['kind'] == 'ready':
                    counts['ready'] += 1
                    build_id = event['build_id']
                    ready_raw = event['raw']
                    status = 'board_ready'
                    print(event['raw'], flush=True)
                    send(event)
                    continue
                counts['valid_events'] += 1
                outcome, gap = dedup.accept(event)
                counts[outcome] += 1
                if outcome != 'accepted':
                    evidence.write('rejected_identity', reason=outcome, message=event)
                    continue
                if gap:
                    counts['missing_ids'] += gap
                    evidence.write('event_gap', missing=gap, message=event)
                status = 'receiving'
                if send(event):
                    counts['forwarded_events'] += 1
                else:
                    counts['not_forwarded'] += 1
                    evidence.write('not_forwarded', message=event, reason='no TCP client; never replay')
                print('BTE1', event['session'], event['event_id'], dict(counts), flush=True)
            if args.start_app and not sent_start and not seen_board and re.sub(rb'\x1b\[[0-9;]*[A-Za-z]', b'', prompt_tail).rstrip().endswith(b'nsh>'):
                command = b'openvela_ble_probe --ui-paused --diag-off -c\n'
                port.write(command)
                sent_start = True
                evidence.write('serial_write', command=command.decode('ascii'), reason='observed NSH prompt; one-shot approved app start')
                print('Sent approved start command once at NSH prompt.', flush=True)
    except KeyboardInterrupt:
        evidence.write('operator_stop')
    except Exception as exc:
        evidence.write('failure', error=repr(exc))
        print('ERROR:', exc, flush=True)
        return 1
    finally:
        send(dict(kind='status', state='disconnected'))
        if sock is not None:
            sock.close()
        if port.is_open:
            port.close()
        counts.update(framer.counts)
        evidence.write('summary', counts=dict(counts), elapsed_seconds=time.monotonic()-started)
        (evidence.path/'summary.json').write_text(json.dumps(dict(source='board_serial', counts=dict(counts)),indent=2),encoding='utf-8')
        evidence.raw.close()
        evidence.log.close()
        dedup.db.close()
        print('Stopped host bridge only. Evidence:', evidence.path, flush=True)
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
