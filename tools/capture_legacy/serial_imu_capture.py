#!/usr/bin/env python3
"""Bounded host recording for the foreground @IMU1 firmware (no firmware stop)."""
import argparse
import csv
import json
import math
import queue
import re
import struct
import threading
import time
import traceback
from datetime import datetime
from pathlib import Path

VERSION = '2.2.0'
PREFIX = b'@IMU1,'
HEADER = ['received_at_local', 'received_epoch_us', 'record_type',
          'device_sequence', 'expected_sequence', 'missing_count',
          'device_uptime_ms', 'raw_x', 'raw_y', 'raw_z', 'x_mg', 'y_mg', 'z_mg',
          'loss_reason', 'last_sample', 'serial_line', 'session_id',
          'host_monotonic_ns', 'capture_phase']
IMU_HEADER = HEADER + ['protocol_version', 'raw_ax', 'raw_ay', 'raw_az',
                       'raw_gx', 'raw_gy', 'raw_gz', 'flags_raw',
                       'estimated_time', 'firmware_gap_flag', 'warmup', 'analysis_phase']


def crc16(data):
    crc = 0xffff
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0xa001 if crc & 1 else crc >> 1
    return crc


def parse(line):
    """Parse only protocol lines; control records must precede numeric parsing."""
    if not line.startswith('@IMU1,'):
        return None
    fields = line.strip().split(',')
    if len(fields) > 1 and fields[1] == 'READY':
        if len(fields) < 7 or fields[2] not in ('1', '2') or fields[5] != '4g':
            raise ValueError('unsupported READY')
        sid = int(fields[3])
        if not 0 <= sid <= 0xffffffff or float(fields[4]) != 104:
            raise ValueError('invalid READY session/rate')
        return {'type': 'ready', 'session': sid, 'rate_hz': 104,
                'time_description': fields[6], 'ready_version': int(fields[2]),
                'extra_fields': fields[7:], 'line': line}
    if len(fields) > 1 and fields[1] == 'FAIL':
        return {'type': 'fail', 'line': line}
    if (len(fields) != 4 or fields[1] not in ('20', '27')
            or re.fullmatch(r'[0-9a-fA-F]+', fields[2]) is None
            or len(fields[2]) != int(fields[1])*2
            or re.fullmatch(r'[0-9a-fA-F]{4}', fields[3]) is None):
        raise ValueError('bad frame length/hex')
    body = bytes.fromhex(fields[2])
    if crc16(body) != int(fields[3], 16):
        raise ValueError('CRC mismatch')
    if len(body) == 27 and body[:2] == b'\x02\x02':
        sid, seq, uptime, x, y, z, gx, gy, gz, flags = struct.unpack('<IIIhhhhhhB', body[2:])
        return {'type': 'sample', 'session': sid, 'sequence': seq,
                'uptime': uptime, 'xyz': (x, y, z), 'gyro': (gx, gy, gz),
                'flags': flags, 'protocol_version': 2}
    if len(body) != 20 or body[:2] != b'\x01\x01':
        raise ValueError('unsupported version/type')
    sid, seq, uptime, x, y, z = struct.unpack('<IIIhhh', body[2:])
    return {'type': 'sample', 'session': sid, 'sequence': seq,
            'uptime': uptime, 'xyz': (x, y, z), 'protocol_version': 1}


class Framer:
    """Bounded line buffer, recovering a prefix after unrelated console text."""
    def __init__(self):
        self.buffer = b''
        self.overflows = 0

    def feed(self, data):
        self.buffer += data
        lines = []
        while b'\n' in self.buffer:
            line, self.buffer = self.buffer.split(b'\n', 1)
            if len(line) > 4096:
                self.overflows += 1
                line = line[-4096:]
            lines.append(line.decode('utf-8', errors='replace').rstrip('\r'))
        if len(self.buffer) > 4096:
            self.overflows += 1
            self.buffer = self.buffer[-4096:]
        return lines


class Sequence:
    def __init__(self):
        self.expected = None
        self.last = None

    def accept(self, seq):
        expected = seq if self.expected is None else self.expected
        delta = (seq - expected) & 0xffffffff
        reason = ''
        missing = 0
        if delta >= 0x80000000:
            reason = 'duplicate' if seq == self.last else 'out_of_order'
        else:
            missing = delta
            reason = 'sequence_gap' if delta else ''
            self.expected = (seq + 1) & 0xffffffff
            self.last = seq
        return expected, missing, reason


def run(args):
    # Refuse overwriting evidence, but create a missing parent directory.
    args.output_dir.parent.mkdir(parents=True, exist_ok=True)
    args.output_dir.mkdir(exist_ok=False)
    root = args.output_dir
    stop = threading.Event()
    camera_ready = threading.Event()
    prompt = threading.Event()
    ready = threading.Event()
    start_sent = threading.Event()
    serial_handles = queue.Queue()
    failures = queue.Queue()
    window = {'start': None, 'end': None}
    warmup_s = getattr(args, 'warmup_s', 1.0)
    require_six_axis = getattr(args, 'require_six_axis', False)
    stats = {'sample_count': 0, 'capture_sample_count': 0, 'missing_count': 0,
             'duplicate_count': 0, 'out_of_order_count': 0, 'parse_errors': 0,
             'framing_overflows': 0, 'video_frames': 0, 'ready': None,
             'last_sequence': None, 'stop_summary': None,
             'six_axis_sample_count': 0, 'warmup_sample_count': 0,
             'firmware_gap_flag_count': 0, 'unknown_flag_count': 0,
             'protocol_counts': {}, 'ready_host_monotonic_ns': None}
    meta = {'script_version': VERSION, 'session_status': 'initializing',
            'training_data_valid': False, 'training_data_review_required': True,
            'serial_port': args.serial_port, 'baud': args.baud,
            'duration_requested_s': args.duration_s,
            'hardware_synchronized': False,
            'device_time_semantics': 'FIFO estimate; v2 bit0 ESTIMATED_TIME; v1 has no per-sample flags',
            'warmup': {'seconds_after_ready_host_receipt': warmup_s,
                       'note': 'Experimental exclusion window; raw samples retained; not a universal settling guarantee'},
            'require_six_axis': require_six_axis,
            'stationary_test': getattr(args, 'stationary_test', False),
            'gyro_units': 'raw signed int16; no bias correction or physical scaling applied',
            'magnetometer': 'not provided by this protocol',
            'firmware_gap_semantics': 'v2 bit1 preserved separately from observed sequence gaps; known pre-fix serial firmware may set it due to inactive Game queue',
            'host_time_semantics': 'Serial chunk read completion; camera read completion, not exposure timestamp',
            'firmware_stop_supported': False, 'firmware_stop_confirmed': False,
            'board_reset_required': False, 'errors': [], 'cleanup_errors': [],
            'video_timing': 'video_capture.mp4 preserves captured frames; video.mp4 rebuilt from read timestamps with held repeats',
            'video_frames_csv_indexes': 'video_capture.mp4; video.timeline.csv maps the viewing copy'}

    def error(worker, exc):
        failures.put({'worker': worker, 'type': type(exc).__name__,
                      'message': str(exc), 'traceback': traceback.format_exc()})

    def phase(ns):
        if window['start'] is None or ns < window['start']:
            return 'startup'
        return 'capture' if ns < window['end'] else 'after_capture'

    def serial_worker():
        ser = None
        framer = Framer()
        sequence = Sequence()
        prompt_tail = b''
        try:
            import serial
            with (root / 'serial_raw.bin').open('wb') as raw, \
                 (root / 'serial_decoded.log').open('w', encoding='utf-8') as decoded, \
                 (root / 'parse_errors.log').open('w', encoding='utf-8') as bad, \
                 (root / 'raw_accel.csv').open('w', newline='', encoding='utf-8') as cf, \
                 (root / 'raw_imu.csv').open('w', newline='', encoding='utf-8') as imuf:
                writer = csv.writer(cf)
                writer.writerow(HEADER)
                imu_writer = csv.writer(imuf)
                imu_writer.writerow(IMU_HEADER)
                # Avoid intentionally toggling reset/boot pins when opening COM7.
                ser = serial.Serial(port=None, baudrate=args.baud, timeout=.05,
                                    write_timeout=2, rtscts=False, dsrdtr=False)
                ser.rts = False
                ser.dtr = False
                ser.port = args.serial_port
                ser.open()
                serial_handles.put(ser)
                while not stop.is_set():
                    chunk = ser.read(min(4096, ser.in_waiting or 1))
                    if not chunk:
                        continue
                    ns, epoch = time.monotonic_ns(), time.time_ns() // 1000
                    raw.write(chunk)
                    prompt_tail = (prompt_tail + chunk)[-256:]
                    if b'nsh>' in prompt_tail:
                        prompt.set()
                    for line in framer.feed(chunk):
                        decoded.write(f'{epoch} {line}\n')
                        if 'IMU1 STOP ' in line:
                            stats['stop_summary'] = line
                            if ready.is_set() and not stop.is_set():
                                raise RuntimeError('Firmware stopped before host recording ended')
                        offset = line.rfind('@IMU1,')
                        if offset < 0:
                            continue
                        protocol = line[offset:]
                        try:
                            record = parse(protocol)
                        except (ValueError, struct.error) as exc:
                            stats['parse_errors'] += 1
                            bad.write(f'{epoch} {exc!r} {line!r}\n')
                            continue
                        if not start_sent.is_set():
                            raise RuntimeError('IMU stream already active; reset board to NSH first')
                        if record['type'] == 'fail':
                            raise RuntimeError(record['line'])
                        if record['type'] == 'ready':
                            if ready.is_set():
                                raise RuntimeError('Repeated READY / unexpected session restart')
                            stats['ready'] = record
                            stats['ready_host_monotonic_ns'] = ns
                            ready.set()
                            continue
                        if not ready.is_set():
                            raise RuntimeError('Sample received before READY')
                        if record['session'] != stats['ready']['session']:
                            raise RuntimeError('Sample session differs from READY')
                        if require_six_axis and record['protocol_version'] != 2:
                            raise RuntimeError('Six-axis required, but firmware sent legacy acceleration-only frame')
                        seq = record['sequence']
                        expected, missing, reason = sequence.accept(seq)
                        x, y, z = record['xyz']
                        cp = phase(ns)
                        base_row = [datetime.fromtimestamp(epoch / 1e6).astimezone().isoformat(),
                                         epoch, 'sample', seq, expected, missing, record['uptime'],
                                         x, y, z, x * .122, y * .122, z * .122,
                                         reason, '', protocol, record['session'], ns, cp]
                        writer.writerow(base_row)
                        warming = ns < stats['ready_host_monotonic_ns'] + int(warmup_s*1e9)
                        pv = str(record['protocol_version'])
                        stats['protocol_counts'][pv] = stats['protocol_counts'].get(pv, 0)+1
                        stats['warmup_sample_count'] += int(warming)
                        if record['protocol_version'] == 2:
                            flags = record['flags']
                            imu_writer.writerow(base_row + [2, x, y, z, *record['gyro'], flags,
                                int(bool(flags & 1)), int(bool(flags & 2)), int(warming),
                                'warmup' if warming else cp])
                            stats['six_axis_sample_count'] += 1
                            stats['firmware_gap_flag_count'] += int(bool(flags & 2))
                            stats['unknown_flag_count'] += int(bool(flags & ~3))
                        stats['sample_count'] += 1
                        stats['capture_sample_count'] += int(cp == 'capture')
                        stats['missing_count'] += missing
                        stats['duplicate_count'] += int(reason == 'duplicate')
                        stats['out_of_order_count'] += int(reason == 'out_of_order')
                        stats['last_sequence'] = sequence.last
                    stats['framing_overflows'] = framer.overflows
                meta['serial_trailing_bytes'] = len(framer.buffer)
        except BaseException as exc:
            error('serial', exc)
        finally:
            if ser is not None:
                try:
                    ser.close()
                except Exception as exc:
                    error('serial_close', exc)

    def camera_worker():
        camera = output = None
        try:
            import cv2
            camera = cv2.VideoCapture(args.camera_index, cv2.CAP_DSHOW)
            if not camera.isOpened():
                raise RuntimeError('Camera open failed')
            ok, frame = camera.read()
            if not ok or frame is None:
                raise RuntimeError('Camera first frame failed')
            height, width = frame.shape[:2]
            fps = float(camera.get(cv2.CAP_PROP_FPS))
            if not math.isfinite(fps) or not 1 <= fps <= 240:
                fps = 30.0
            output = cv2.VideoWriter(str(root / 'video_capture.mp4'),
                                     cv2.VideoWriter_fourcc(*'mp4v'), fps, (width, height))
            if not output.isOpened():
                raise RuntimeError('Video encoder open failed')
            meta['camera'] = {'index': args.camera_index, 'width': width,
                              'height': height, 'encoding_fps': fps}
            with (root / 'video_frames.csv').open('w', newline='', encoding='utf-8') as vf:
                writer = csv.writer(vf)
                writer.writerow(['frame_index', 'host_monotonic_ns', 'host_epoch_us'])
                camera_ready.set()
                while not stop.is_set():
                    ok, frame = camera.read()
                    ns, epoch = time.monotonic_ns(), time.time_ns() // 1000
                    if not ok or frame is None:
                        raise RuntimeError('Camera frame read failed')
                    if phase(ns) != 'capture':
                        continue
                    if frame.shape[:2] != (height, width):
                        raise RuntimeError('Camera dimensions changed')
                    output.write(frame)
                    writer.writerow([stats['video_frames'], ns, epoch])
                    stats['video_frames'] += 1
            meta['video_capture_encoded_duration_s'] = stats['video_frames'] / fps
        except BaseException as exc:
            error('camera', exc)
        finally:
            for name, resource in [('video_release', output), ('camera_release', camera)]:
                if resource is not None:
                    try:
                        resource.release()
                    except Exception as exc:
                        error(name, exc)

    def check():
        if not failures.empty():
            failure = failures.get_nowait()
            meta['errors'].append(failure)
            raise RuntimeError(f"{failure['worker']}: {failure['message']}")

    def wait_for(event, seconds, description):
        deadline = time.monotonic() + seconds
        while not event.wait(.05):
            check()
            if time.monotonic() >= deadline:
                raise TimeoutError(description)
        check()

    threads = []
    try:
        print(f'Evidence: {root.resolve()}', flush=True)
        for name, target in [('serial', serial_worker), ('camera', camera_worker)]:
            thread = threading.Thread(name=name, target=target, daemon=True)
            thread.start()
            threads.append(thread)
        deadline = time.monotonic() + 10
        while serial_handles.empty():
            check()
            if time.monotonic() > deadline:
                raise TimeoutError('Serial open timeout')
            time.sleep(.05)
        ser = serial_handles.get_nowait()
        # A blank line only asks NSH for its prompt; never launch twice blindly.
        ser.write(b'\r\n')
        wait_for(prompt, 10, 'No NSH prompt. Reset board, leave it at NSH, and rerun in a new directory.')
        wait_for(camera_ready, 15, 'Camera initialization timeout')
        check()
        print('[start] Sending openvela_ble_probe; waiting for READY', flush=True)
        start_sent.set()
        meta['board_reset_required'] = True
        ser.write(b'openvela_ble_probe\r\n')
        wait_for(ready, args.ready_timeout_s, 'No valid READY received')
        window['end'] = time.monotonic_ns() + int(args.duration_s * 1e9)
        window['start'] = window['end'] - int(args.duration_s * 1e9)
        if getattr(args, 'stationary_test', False):
            print('[capture] READY. Stationary test: leave the board untouched for the entire recording; no downbeats.', flush=True)
        else:
            print('[capture] READY. Hold still for 2 seconds, then make three clear downbeats.', flush=True)
        print('Video and IMU are not hardware synchronized.', flush=True)
        meta['session_status'] = 'recording'
        last_print = time.monotonic()
        while time.monotonic_ns() < window['end']:
            check()
            time.sleep(.05)
            if time.monotonic() - last_print >= 5:
                print(f"[capture] samples={stats['capture_sample_count']} video_frames={stats['video_frames']}", flush=True)
                last_print = time.monotonic()
        check()
        meta['session_status'] = 'host_recording_complete'
    except BaseException as exc:
        meta['session_status'] = 'failed'
        meta['errors'].append({'type': type(exc).__name__, 'message': str(exc),
                               'traceback': traceback.format_exc()})
        print(f'Capture failed: {type(exc).__name__}: {exc}', flush=True)
    finally:
        stop.set()
        for thread in threads:
            thread.join(timeout=3)
            if thread.is_alive():
                meta['cleanup_errors'].append(f'{thread.name} worker did not stop; its files may be incomplete')
        while not failures.empty():
            meta['errors'].append(failures.get_nowait())
        meta['statistics'] = dict(stats)
        meta['capture_window_monotonic_ns'] = dict(window)
        if meta['errors'] or meta['cleanup_errors'] or not stats['capture_sample_count'] or not stats['video_frames']:
            meta['session_status'] = 'failed'
        meta['parse_and_sequence_clean'] = not any(stats[k] for k in
            ['parse_errors', 'framing_overflows', 'missing_count', 'duplicate_count', 'out_of_order_count'])
        meta['firmware_flags_review_required'] = bool(stats['firmware_gap_flag_count'] or stats['unknown_flag_count'])
        meta['sample_rate_observed_hz'] = stats['capture_sample_count'] / args.duration_s
        meta['video_frame_rate_observed_hz'] = stats['video_frames'] / args.duration_s
        meta['host_end_time'] = datetime.now().astimezone().isoformat()
        # Native read/write calls can block; never touch video files still owned by a worker.
        if (meta['session_status'] == 'host_recording_complete'
                and not any(t.is_alive() for t in threads)):
            try:
                from video_timeline import repair
                print('[video] Rebuilding viewing copy from camera timestamps; preserving captured frames.', flush=True)
                meta['video_timeline'] = repair(root / 'video_capture.mp4', root / 'video_frames.csv',
                                                meta, root / 'video.mp4')
            except BaseException as exc:
                meta['session_status'] = 'failed'
                meta['errors'].append({'type': type(exc).__name__, 'message': str(exc),
                                       'stage': 'video_timeline', 'traceback': traceback.format_exc()})
        (root / 'session_metadata.json').write_text(json.dumps(meta, ensure_ascii=False, indent=2), encoding='utf-8')
        print(f"Result: {meta['session_status']}; samples={stats['capture_sample_count']}; frames={stats['video_frames']}", flush=True)
        if meta['board_reset_required']:
            print('Firmware has NO stop command. Host recording ended; reset the board before another run.', flush=True)
        print('Files require review; this result does not certify training data.', flush=True)
    return 0 if meta['session_status'] == 'host_recording_complete' else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--serial-port', default='COM7')
    ap.add_argument('--baud', type=int, default=1000000)
    ap.add_argument('--camera-index', type=int, default=0)
    ap.add_argument('--duration-s', type=float, default=30)
    ap.add_argument('--ready-timeout-s', type=float, default=20)
    ap.add_argument('--warmup-s', type=float, default=1.0,
                    help='Mark samples within this interval after READY as warm-up; keep all raw data')
    ap.add_argument('--require-six-axis', action='store_true',
                    help='Fail if the firmware sends legacy acceleration-only samples')
    ap.add_argument('--stationary-test', action='store_true',
                    help='Record a stationary test without asking for synchronization gestures')
    ap.add_argument('--output-dir', type=Path, required=True)
    args = ap.parse_args()
    if not math.isfinite(args.duration_s) or args.duration_s <= 0:
        ap.error('--duration-s must be finite and positive')
    if not math.isfinite(args.ready_timeout_s) or args.ready_timeout_s <= 0:
        ap.error('--ready-timeout-s must be finite and positive')
    if not math.isfinite(args.warmup_s) or args.warmup_s < 0:
        ap.error('--warmup-s must be finite and nonnegative')
    return run(args)


if __name__ == '__main__':
    raise SystemExit(main())
