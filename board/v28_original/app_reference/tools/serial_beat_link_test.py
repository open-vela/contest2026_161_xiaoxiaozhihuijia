#!/usr/bin/env python3
"""Cold-start serial BEAT1 loop test; not an action recognizer."""
import argparse, struct, time, sys
from pathlib import Path
import serial

def crc16(data):
    c = 0xffff
    for b in data:
        c ^= b
        for _ in range(8): c = (c >> 1) ^ 0xa001 if c & 1 else c >> 1
    return c

def frame(session, event):
    p = struct.pack('<BBII', 1, 1, session, event)
    return f'@BEAT1,10,{p.hex()},{crc16(p):04x}\n'.encode()

def parse_ready(line):
    if not line.startswith(b'@IMU1,READY,'): return None
    try:
        p = line.decode('ascii').strip().split(',')
        if 'beat_protocol=BEAT1' not in p: return None
        build = next((x.split('=',1)[1] for x in p if x.startswith('build_id=')), '')
        return int(p[3], 10), build
    except (ValueError, IndexError): return None

def parse_ack(line):
    if not line.startswith(b'@BEAT1_ACK,'): return None
    p = line.decode('ascii').strip().split(',')
    if len(p) != 5: return None
    try: return tuple(int(x, 10) for x in p[1:5])
    except ValueError: return None

def parse_failure(line):
    return (b'IMU1 FAIL' in line or b'IMU raw open ' in line or
            b'IMU1 STOP' in line or line.strip() == b'nsh>')

def host_log(diag, text):
    line = f"#HOST {time.strftime('%Y-%m-%dT%H:%M:%S')} {text}\n".encode()
    diag.write(line.decode()); diag.flush(); print(line.decode().rstrip(), file=sys.stderr)

def validate_args(a):
    if a.rts_reset_pulse_ms <= 0: raise SystemExit('--rts-reset-pulse-ms must be positive')
    if a.startup_timeout_s <= 0: raise SystemExit('--startup-timeout-s must be positive')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--serial-port', default='COM7'); ap.add_argument('--baud', type=int, default=1000000)
    ap.add_argument('--events', type=int, default=10); ap.add_argument('--reset-via-rts', action='store_true')
    ap.add_argument('--rts-reset-pulse-ms', type=float, default=100.0)
    ap.add_argument('--startup-timeout-s', type=float, default=30.0)
    ap.add_argument('--ack-timeout-s', type=float, default=4); ap.add_argument('--duplicate-event', type=int, default=3)
    ap.add_argument('--raw-log', default='beat_link_raw.bin'); ap.add_argument('--diag-log', default='beat_link_diag.log')
    ap.add_argument('--startup-command', default='openvela_ble_probe\n')
    a = ap.parse_args()
    validate_args(a)
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = a.serial_port, a.baud, .1
    s.rtscts = False
    s.rts = False
    raw = Path(a.raw_log).open('wb'); diag = Path(a.diag_log).open('w', encoding='utf-8')
    try:
      s.open()
      with raw, diag:
        buf = b''; session = None; build_id = ''; prompt = False; failure = None
        def read_until(deadline, stop_prompt=False):
            nonlocal buf, session, build_id, prompt, failure
            while time.monotonic() < deadline:
                b = s.read(512)
                if b: raw.write(b); buf += b
                if b'nsh>' in buf:
                    prompt = True
                while b'\n' in buf:
                    line, buf = buf.split(b'\n', 1)
                    prompt = prompt or b'nsh>' in line
                    q = parse_ready(line + b'\n')
                    if q is not None: session, build_id = q
                    if parse_failure(line): failure = line.decode(errors='replace').strip()
                if session is not None or (prompt and stop_prompt):
                    return session
            return session
        if a.reset_via_rts:
            try:
                s.rts = True; host_log(diag, 'RTS=True result=ok')
                time.sleep(a.rts_reset_pulse_ms / 1000.0)
            except Exception as exc:
                host_log(diag, f'RTS=True result=failed error={exc!r}')
                raise
            finally:
                try:
                    s.rts = False; host_log(diag, 'RTS=False result=ok release=finally')
                except Exception as exc:
                    host_log(diag, f'RTS=False result=failed error={exc!r}')
            prompt = False; session = None
            startup_deadline = time.monotonic() + a.startup_timeout_s
            read_until(startup_deadline, stop_prompt=True)
        else:
            read_until(time.monotonic() + a.startup_timeout_s)
        if session is None and prompt:
            prompt = False
            buf = b''
            tx = a.startup_command.encode('ascii'); n = s.write(tx)
            host_log(diag, f'startup_command sent count=1 tx_hex={tx.hex()} write_len={n}')
            read_until(startup_deadline if a.reset_via_rts else time.monotonic() + a.startup_timeout_s)
        if session is None:
            reason = f'；原因={failure}' if failure else ''
            raise SystemExit(f'未收到READY{reason}；请保留原始日志。')
        print(f'session={session} build_id={build_id}')
        ids = list(range(1, a.events + 1)); ids.insert(min(max(a.duplicate_event - 1, 0), len(ids)), a.duplicate_event)
        seen = []; accepted = 0; processed = set(); duplicate_seen = False
        for pos, event in enumerate(ids):
            ok = False
            for attempt in range(1, 3):
                prior_count = accepted
                tx = frame(session, event); t0 = time.monotonic(); n = s.write(tx)
                host_log(diag, f'tx session={session} event_id={event} attempt={attempt} mono={t0:.6f} tx_hex={tx.hex()} write_len={n}')
                deadline = time.monotonic() + a.ack_timeout_s
                while time.monotonic() < deadline:
                    b = s.read(512)
                    if b: raw.write(b); buf += b
                    while b'\n' in buf:
                        line, buf = buf.split(b'\n', 1); q = parse_ack(line)
                        host_log(diag, f'rx raw={line.hex()} text={line.decode(errors="replace")!r}')
                        if q is None:
                            host_log(diag, 'ack ignored reason=malformed_or_unrelated'); continue
                        if q[0] != session or q[1] != event:
                            host_log(diag, f'ack ignored reason=session_or_event_mismatch parsed={q}'); continue
                        if q[2] in (2, 3):
                            host_log(diag, f'ack rejected status={q[2]}'); continue
                        if q[3] < accepted:
                            host_log(diag, f'ack ignored reason=count_regression parsed={q}'); continue
                        seen.append(q); accepted = q[3]
                        if q[2] == 0: processed.add(event)
                        elif q[2] == 1 and (event in processed or q[3] > prior_count):
                            processed.add(event); duplicate_seen |= (event == a.duplicate_event)
                        ok = (q[2] == 0) or (q[2] == 1 and event in processed)
                        break
                    if ok: break
                if ok: break
            if not ok: raise SystemExit(f'ACK timeout event_id={event}; raw log retained')
        if accepted != a.events or len(processed) != a.events or not duplicate_seen:
            raise SystemExit(f'ACK acceptance mismatch accepted_count={accepted} processed={len(processed)}; raw log retained')
        print(f'session={session} acknowledgements={len(seen)} accepted_count={accepted} raw_log={a.raw_log}')
        for q in seen: print(f'ACK session={q[0]} event={q[1]} status={q[2]} accepted_count={q[3]}')
    finally:
        try:
            if s.is_open and a.reset_via_rts: s.rts = False
        finally:
            if s.is_open: s.close()
if __name__ == '__main__': main()
