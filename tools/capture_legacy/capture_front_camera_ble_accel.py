#!/usr/bin/env python3
"""Record front-camera video and raw BLE accelerometer samples."""

import argparse
import asyncio
import codecs
import csv
import importlib.util
import json
import linecache
import struct
import sys
import time
import traceback
import zlib
from datetime import datetime, timezone
from pathlib import Path

SCRIPT_VERSION = "1.2.4"
SERVICE_UUID = "0000fff0-0000-1000-8000-00805f9b34fb"
RX_UUID = "0000fff1-0000-1000-8000-00805f9b34fb"
TX_UUID = "0000fff2-0000-1000-8000-00805f9b34fb"
CMD_BINARY_PING, FRAME_BINARY_PONG = 0x01, 0x81
CMD_TIME_SYNC, CMD_CAPTURE_START, CMD_CAPTURE_STOP = 0x40, 0x30, 0x31
FRAME_SAMPLE, FRAME_CONTROL, PROTOCOL_VERSION = 0x02, 0x03, 1
ACCEL_MG_PER_LSB = 0.122
CSV_HEADER = [
    "received_at_local", "received_epoch_us", "record_type",
    "device_sequence", "expected_sequence", "missing_count",
    "device_uptime_ms", "raw_x", "raw_y", "raw_z", "x_mg", "y_mg",
    "z_mg", "loss_reason", "last_sample", "serial_line",
]


def require_packages():
    missing = [name for module, name in
               (("cv2", "opencv-python"), ("bleak", "bleak"))
               if importlib.util.find_spec(module) is None]
    if missing:
        print("Missing required Python package(s): " + ", ".join(missing)
              + ". Install explicitly on Windows with: py -m pip install "
              + " ".join(missing), file=sys.stderr)
        raise SystemExit(2)


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True)
    parser.add_argument("--camera-index", type=int, default=0)
    parser.add_argument("--scan-timeout-s", type=float, default=10.0)
    parser.add_argument("--connect-timeout-s", type=float, default=20.0)
    parser.add_argument("--control-timeout-s", type=float, default=5.0)
    parser.add_argument("--ping-preflight", action="store_true")
    parser.add_argument("--ping-timeout-s", type=float, default=5.0)
    parser.add_argument("--ack-only-test", action="store_true",
                        help="Label this run as an A firmware control-only diagnostic; expect zero samples")
    parser.add_argument("--winrt-service-changed-retry", action="store_true",
                        help="Diagnostic workaround: enable the WinRT backend's existing service-change discovery retry")
    parser.add_argument("--duration-s", type=float, default=30.0)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--session-id",
                        default=datetime.now().strftime("%Y%m%d_%H%M%S"))
    parser.add_argument("--meter", required=True)
    parser.add_argument("--bpm", type=float, required=True)
    parser.add_argument("--hand", choices=["right"], default="right")
    parser.add_argument("--watch-face", choices=["back-of-hand"],
                        default="back-of-hand")
    parser.add_argument("--serial-port")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    for option in ("duration_s", "scan_timeout_s", "connect_timeout_s",
                   "control_timeout_s", "ping_timeout_s"):
        if getattr(args, option) <= 0:
            parser.error("--" + option.replace("_", "-") + " must be positive")
    return args


def local_timestamp(epoch_ns):
    return datetime.fromtimestamp(epoch_ns / 1e9, timezone.utc).astimezone().isoformat(
        timespec="microseconds")


def winrt_query_identity(frame, source, cache):
    """Read identity properties only; never issue a GATT value read or query."""
    if frame.f_code.co_name != "_get_services":
        return {}
    # Restrict identities to the suspended call, not leftover loop variables.
    callsite = "\n".join(source.splitlines()[:4])
    names = []
    if "service.get_characteristics" in callsite:
        names = ["service"]
    elif "characteristic.get_descriptors" in callsite:
        names = ["service", "characteristic"]
    identities = {}
    for name in names:
        obj = frame.f_locals.get(name)
        if obj is None:
            identities[name] = {"unavailable": "local object absent"}
            continue
        key = id(obj)
        cached = cache.get(key)
        if cached is not None and cached[0] is obj:
            identities[name] = cached[1]
            continue
        if len(cache) >= 128:
            identities[name] = {"unavailable": "identity cache capacity reached"}
            continue
        identity = {}
        for attribute in ("uuid", "attribute_handle"):
            try:
                value = getattr(obj, attribute)
                identity[attribute] = str(value) if attribute == "uuid" else int(value)
            except Exception as exc:
                identity[attribute + "_error"] = type(exc).__name__
        cache[key] = (obj, identity)
        identities[name] = identity
    return identities


def winrt_pending_frames(identity_cache=None):
    """Observe suspended frames and query identities, never attribute values."""
    if identity_cache is None:
        identity_cache = {}
    found = []
    for task in asyncio.all_tasks():
        current, seen = task.get_coro(), set()
        while current is not None and id(current) not in seen:
            seen.add(id(current))
            frame = getattr(current, "cr_frame", None)
            if frame is None:
                frame = getattr(current, "gi_frame", None)
            if frame is not None:
                filename = frame.f_code.co_filename
                if filename.replace("\\", "/").endswith("/bleak/backends/winrt/client.py"):
                    lineno = frame.f_lineno
                    source = "".join(linecache.getline(filename, n)
                                     for n in range(lineno, lineno + 10))
                    found.append({
                        "task_id": hex(id(task)),
                        "function": frame.f_code.co_name,
                        "file": filename, "line": lineno,
                        "source_context": source,
                        "query_identity": winrt_query_identity(frame, source, identity_cache),
                    })
            following = getattr(current, "cr_await", None)
            if following is None:
                following = getattr(current, "gi_yieldfrom", None)
            current = following
    return sorted(found, key=lambda item: (item["task_id"], item["function"], item["line"]))


async def connect_with_wait_diagnostics(client, timeout, diagnostic):
    """Sample pending queries without patching Bleak or changing its deadline."""
    started = time.monotonic()
    last_snapshot = []
    changes = 0
    identity_cache = {}

    async def observe():
        nonlocal last_snapshot, changes
        try:
            while True:
                snapshot = winrt_pending_frames(identity_cache)
                if snapshot != last_snapshot:
                    last_snapshot = snapshot
                    changes += 1
                    if changes <= 128:
                        diagnostic("winrt_pending_snapshot", elapsed_s=time.monotonic() - started,
                                   frames=snapshot)
                        print("[winrt:pending] " + json.dumps(snapshot, ensure_ascii=False), flush=True)
                await asyncio.sleep(0.25)
        except Exception as exc:
            # An observer failure must not replace the connection result.
            print(f"[winrt:observer_error] {type(exc).__name__}: {exc!r}", flush=True)

    observer = asyncio.create_task(observe())
    outcome = "failed_or_cancelled"
    try:
        result = await asyncio.wait_for(client.connect(), timeout)
        outcome = "connected"
        return result
    finally:
        observer.cancel()
        try:
            await observer
        except asyncio.CancelledError:
            pass
        identity_cache.clear()
        try:
            diagnostic("winrt_pending_summary", outcome=outcome,
                       elapsed_s=time.monotonic() - started, changes=changes,
                       snapshots_omitted=max(0, changes - 128), last_snapshot=last_snapshot,
                       limitation="250 ms sampled pending frames, not a complete API call trace or proof of ATT transmission")
        except Exception as exc:
            print(f"[winrt:observer_summary_error] {exc!r}", flush=True)


def empty_row(record_type, epoch_ns=None, **values):
    epoch_ns = epoch_ns or time.time_ns()
    row = {name: "" for name in CSV_HEADER}
    row.update(received_at_local=local_timestamp(epoch_ns),
               received_epoch_us=epoch_ns // 1000,
               record_type=record_type, last_sample=0)
    row.update(values)
    return row


def session_token(session_id):
    return zlib.crc32(session_id.encode()) & 0xFFFFFFFF


def normalize_ble_address(address):
    normalized = str(address).strip().casefold().strip("{}")
    compact = normalized.replace(":", "").replace("-", "")
    return compact if len(compact) == 12 and all(
        c in "0123456789abcdef" for c in compact) else normalized


def exception_codes(exc):
    found, seen = [], set()
    current = exc
    while current is not None and id(current) not in seen:
        seen.add(id(current))
        for attribute in ("hresult", "winerror"):
            value = getattr(current, attribute, None)
            if isinstance(value, int):
                found.append((attribute, value, f"0x{value & 0xFFFFFFFF:08X}"))
        for value in getattr(current, "args", ()):
            if isinstance(value, int) and (value < 0 or value > 0xFFFF):
                found.append(("args", value, f"0x{value & 0xFFFFFFFF:08X}"))
        current = current.__cause__ or current.__context__
    return list(dict.fromkeys(found))


def log_stage(stage, state, detail=""):
    print(f"[stage:{stage}] {state}" + (f" {detail}" if detail else ""),
          flush=True)


def log_stage_failure(stage, exc):
    detail = f"type={type(exc)!r} repr={exc!r}"
    if exception_codes(exc):
        detail += " windows_codes=" + repr(exception_codes(exc))
    log_stage(stage, "FAILED", detail)


async def scan_for_exact_address(scanner_type, address, timeout):
    scanner = scanner_type()
    await scanner.start()
    try:
        await asyncio.sleep(timeout)
    finally:
        await scanner.stop()
    target = normalize_ble_address(address)
    discovered = scanner.discovered_devices_and_advertisement_data
    matches = [(device, advertisement)
               for device, advertisement in discovered.values()
               if normalize_ble_address(device.address) == target]
    if not matches:
        visible = ", ".join(sorted({
            f"{device.address} ({device.name or 'unknown'})"
            for device, _ in discovered.values()})) or "none"
        raise LookupError(f"target address {address!r} was not found during "
                          f"the {timeout:g}s scan; discovered={visible}")
    for device, advertisement in matches:
        print(f"[scan:match] address={device.address!r} name={device.name!r} "
              f"rssi={getattr(advertisement, 'rssi', None)!r}", flush=True)
    return matches[0][0]


async def discover_probe_service(client):
    services = client.services
    if services.get_service(SERVICE_UUID) is None:
        raise LookupError(f"required GATT service {SERVICE_UUID} was not found")
    if services.get_characteristic(RX_UUID) is None:
        raise LookupError(f"required RX characteristic {RX_UUID} was not found")
    if services.get_characteristic(TX_UUID) is None:
        raise LookupError(f"required TX characteristic {TX_UUID} was not found")


def configure_service_changed_retry(client, requested):
    backend = getattr(client, "_backend", None)
    info = {"requested": bool(requested), "applied": False,
            "backend": (f"{type(backend).__module__}.{type(backend).__name__}"
                        if backend is not None else None),
            "uses_private_backend_option": bool(requested)}
    if requested:
        if backend is None or not hasattr(backend, "_retry_on_services_changed"):
            raise RuntimeError("WinRT Service Changed retry is unavailable in this backend; diagnostic option was not applied")
        backend._retry_on_services_changed = True
        info["applied"] = backend._retry_on_services_changed is True
        if not info["applied"]:
            raise RuntimeError("WinRT Service Changed retry could not be enabled")
    return info


async def capture(args):
    import cv2
    from bleak import BleakClient, BleakScanner
    try:
        import serial
    except ImportError:
        serial = None
    if args.serial_port and serial is None:
        raise RuntimeError("--serial-port requires pyserial; install with: "
                           "py -m pip install pyserial")

    token, loop = session_token(args.session_id), asyncio.get_running_loop()
    sample_queue, control_queue, pong_queue = (asyncio.Queue() for _ in range(3))
    disconnect_event = asyncio.Event()
    state = {"stage": "initializing", "disconnected_ns": None}
    counters = {"samples_received": 0, "first_sequence": None,
                "last_sequence": None}
    board_stats, errors, secondary_errors, camera_info = {}, [], [], {}
    primary_exc = None
    service_changed_retry = {"requested": args.winrt_service_changed_retry,
                             "applied": False}
    session_status, started_ns, ended_ns = "failed", None, None
    capture_started = notify_started = False
    camera = writer = client = serial_device = serial_task = None
    sample_file = frame_file = serial_raw = serial_text = None
    sample_writer = frame_writer = serial_text_writer = None
    paths = {name: args.output_dir / filename for name, filename in {
        "video": "video.mp4", "video_frames": "video_frames.csv",
        "samples": "raw_accel.csv", "metadata": "session_metadata.json",
        "diagnostics": "diagnostics.jsonl", "serial_raw": "serial_raw.bin",
        "serial_text": "serial_decoded.csv"}.items()}

    # A failure directory is diagnostic evidence, explicitly not training data.
    args.output_dir.mkdir(parents=True, exist_ok=False)
    diag = paths["diagnostics"].open("x", encoding="utf-8", buffering=1)

    def diagnostic(event, **fields):
        if diag.closed:
            return
        now = time.time_ns()
        diag.write(json.dumps({"received_at_local": local_timestamp(now),
                               "received_epoch_us": now // 1000,
                               "host_monotonic_ns": time.monotonic_ns(),
                               "event": event, **fields},
                              ensure_ascii=False) + "\n")

    async def stage(name, awaitable, detail=""):
        state["stage"] = name
        log_stage(name, "START")
        diagnostic("stage_start", stage=name)
        try:
            result = await awaitable
        except BaseException as exc:
            log_stage_failure(name, exc)
            diagnostic("stage_failed", stage=name,
                       exception_type=type(exc).__name__, exception_repr=repr(exc),
                       windows_codes=exception_codes(exc))
            raise
        log_stage(name, "OK", detail)
        diagnostic("stage_ok", stage=name, detail=detail)
        return result

    def process_notification(payload, received_ns):
        if diag.closed:
            return
        raw_hex = payload.hex(" ").upper()
        if len(payload) == 17 and payload[0] == FRAME_SAMPLE:
            _, version, flags, sequence, uptime, x, y, z = struct.unpack(
                "<BBBIIhhh", payload)
            counters["samples_received"] += 1
            counters["first_sequence"] = (sequence if
                counters["first_sequence"] is None else counters["first_sequence"])
            counters["last_sequence"] = sequence
            sample_queue.put_nowait({"received_ns": received_ns,
                "version": version, "flags": flags, "sequence": sequence,
                "uptime_ms": uptime, "raw": (x, y, z)})
        elif payload and payload[0] == FRAME_CONTROL:
            parsed, rejection = {}, None
            if len(payload) < 8:
                rejection = "control_too_short"
            elif (len(payload) - 8) % 4:
                rejection = "control_value_bytes_not_u32_aligned"
            else:
                version, subtype, status, frame_token = struct.unpack_from(
                    "<BBBI", payload, 1)
                values = [struct.unpack_from("<I", payload, offset)[0]
                          for offset in range(8, len(payload), 4)]
                parsed = {"version": version, "subtype": subtype,
                          "status": status, "session_token": frame_token,
                          "values": values}
                if version != PROTOCOL_VERSION:
                    rejection = "unsupported_control_version"
                if rejection is None:
                    control_queue.put_nowait({"received_ns": received_ns, **parsed})
            print(f"[control:rx] at={local_timestamp(received_ns)} "
                  f"length={len(payload)} hex={raw_hex} parsed={parsed!r} "
                  f"rejection={rejection!r}", flush=True)
            diagnostic("control_notification", length=len(payload),
                       raw_hex=raw_hex, parsed=parsed,
                       rejection_reason=rejection)
        elif len(payload) == 5 and payload[0] == FRAME_BINARY_PONG:
            sequence = struct.unpack_from("<I", payload, 1)[0]
            pong_queue.put_nowait({"received_ns": received_ns,
                                   "sequence": sequence})
            diagnostic("pong_notification", length=5, raw_hex=raw_hex,
                       sequence=sequence)
        else:
            diagnostic("notification_rejected", length=len(payload),
                       raw_hex=raw_hex, rejection_reason="unknown_frame")

    def notification_handler(_characteristic, data):
        try:
            loop.call_soon_threadsafe(process_notification, bytes(data), time.time_ns())
        except RuntimeError:
            if not loop.is_closed():
                raise

    def disconnected_handler(_client):
        disconnected_ns = time.time_ns()
        def record():
            if diag.closed:
                return
            state["disconnected_ns"] = disconnected_ns
            disconnect_event.set()
            print(f"[ble:disconnected] at={local_timestamp(disconnected_ns)} "
                  f"stage={state['stage']} hci_reason=unknown", flush=True)
            diagnostic("ble_disconnected", stage=state["stage"],
                       hci_reason="unknown")
        try:
            loop.call_soon_threadsafe(record)
        except RuntimeError:
            if not loop.is_closed():
                raise

    async def serial_reader():
        decoder, buffered = codecs.getincrementaldecoder("utf-8")(
            errors="replace"), ""
        while True:
            chunk = await asyncio.to_thread(
                serial_device.read, max(1, serial_device.in_waiting))
            if not chunk:
                await asyncio.sleep(0)
                continue
            now = time.time_ns()
            serial_raw.write(chunk)
            serial_raw.flush()
            buffered += decoder.decode(chunk)
            while "\n" in buffered:
                line, buffered = buffered.split("\n", 1)
                serial_text_writer.writerow([local_timestamp(now), now // 1000,
                                             time.monotonic_ns(),
                                             line.rstrip("\r")])
                serial_text.flush()

    async def wait_matching(queue, predicate, timeout, description):
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                raise TimeoutError(f"timed out waiting for {description}")
            item_task, disconnected_task = (asyncio.create_task(queue.get()),
                                             asyncio.create_task(disconnect_event.wait()))
            done, pending = await asyncio.wait(
                (item_task, disconnected_task), timeout=remaining,
                return_when=asyncio.FIRST_COMPLETED)
            for task in pending:
                task.cancel()
            await asyncio.gather(*pending, return_exceptions=True)
            if not done:
                raise TimeoutError(f"timed out waiting for {description}")
            if disconnected_task in done and disconnected_task.result():
                raise ConnectionError(f"BLE disconnected while waiting for "
                                      f"{description}; HCI reason unknown")
            item = item_task.result()
            if predicate(item):
                return item
            print(f"[wait:reject] description={description!r} item={item!r}",
                  flush=True)
            diagnostic("wait_item_rejected", description=description, item=item)

    async def cleanup(name, operation):
        state["stage"] = name
        log_stage(name, "START")
        try:
            result = operation()
            if asyncio.iscoroutine(result):
                await asyncio.wait_for(result, 5.0)
        except BaseException as exc:
            record = {"step": name, "type": type(exc).__name__,
                      "repr": repr(exc), "windows_codes": exception_codes(exc)}
            secondary_errors.append(record)
            log_stage_failure(name, exc)
            diagnostic("secondary_cleanup_error", **record)
        else:
            log_stage(name, "OK")
            diagnostic("cleanup_ok", step=name)

    print("Protocol note: video and IMU are not hardware-synchronized. "
          "After capture starts, remain still for 2 seconds, then perform "
          "three clear synchronization gestures/downbeats.")
    try:
        if args.serial_port:
            state["stage"] = "serial_open"
            log_stage("serial_open", "START")
            serial_raw = paths["serial_raw"].open("xb", buffering=0)
            serial_text = paths["serial_text"].open(
                "x", newline="", encoding="utf-8", buffering=1)
            serial_text_writer = csv.writer(serial_text)
            serial_text_writer.writerow(["received_at_local", "received_epoch_us",
                                         "host_monotonic_ns", "decoded_text"])
            serial_device = serial.Serial(args.serial_port, args.baud, timeout=0.1)
            serial_task = asyncio.create_task(serial_reader())
            log_stage("serial_open", "OK",
                      f"port={args.serial_port!r} baud={args.baud}")
            diagnostic("serial_reader_started", port=args.serial_port,
                       baud=args.baud)

        device = await stage("scan", scan_for_exact_address(
            BleakScanner, args.address, args.scan_timeout_s),
            f"exact_address={args.address!r}")
        diagnostic("service_discovery_options", use_cached_services=False)
        client = BleakClient(device, disconnected_callback=disconnected_handler,
                             timeout=args.connect_timeout_s,
                             winrt={"use_cached_services": False})
        state["stage"] = "service_changed_retry_configuration"
        service_changed_retry = configure_service_changed_retry(
            client, args.winrt_service_changed_retry)
        diagnostic("service_changed_retry_configuration", **service_changed_retry)
        if service_changed_retry["applied"]:
            print("[winrt] Service Changed discovery retry enabled; one connection attempt", flush=True)
        await stage("connect", connect_with_wait_diagnostics(client,
                    args.connect_timeout_s, diagnostic), f"address={device.address!r}")
        await stage("service_discovery", discover_probe_service(client),
                    f"service={SERVICE_UUID} rx={RX_UUID} tx={TX_UUID}")
        await stage("start_notify", client.start_notify(
            TX_UUID, notification_handler), f"characteristic={TX_UUID}")
        notify_started = True
        sync_tx = bytes([CMD_TIME_SYNC]) + struct.pack(
            "<Q", time.time_ns() // 1_000_000)
        await stage("time_sync", client.write_gatt_char(
            RX_UUID, sync_tx, response=True),
            f"tx_hex={sync_tx.hex(' ').upper()}")

        state["stage"] = "camera_open"
        log_stage("camera_open", "START", f"index={args.camera_index}")
        camera_start = time.monotonic_ns()
        try:
            camera = cv2.VideoCapture(args.camera_index, cv2.CAP_DSHOW)
            if not camera.isOpened():
                camera.release()
                camera = cv2.VideoCapture(args.camera_index)
            if not camera.isOpened():
                raise RuntimeError(f"cannot open camera index {args.camera_index}; "
                                   "verify front camera and Windows permission")
            ok, first_frame = camera.read()
            if not ok or first_frame is None:
                raise RuntimeError("camera opened but returned no valid frame")
        except BaseException as exc:
            log_stage_failure("camera_open", exc)
            raise
        camera_ms = (time.monotonic_ns() - camera_start) / 1e6
        log_stage("camera_open", "OK",
                  f"index={args.camera_index} init_ms={camera_ms:.3f}")
        diagnostic("camera_open", initialization_ms=camera_ms)
        height, width = first_frame.shape[:2]
        fps = float(camera.get(cv2.CAP_PROP_FPS))
        fps = fps if 1 <= fps <= 240 else 30.0
        camera_info = {"requested_index": args.camera_index,
                       "intended_facing": "front",
                       "backend": camera.getBackendName() if hasattr(
                           camera, "getBackendName") else "",
                       "width": width, "height": height, "fps": fps,
                       "fourcc": int(camera.get(cv2.CAP_PROP_FOURCC)),
                       "initialization_ms": camera_ms}

        if args.ping_preflight:
            ping_tx = struct.pack("<BI", CMD_BINARY_PING, token)
            await stage("ping_write", client.write_gatt_char(
                RX_UUID, ping_tx, response=True),
                f"tx_hex={ping_tx.hex(' ').upper()} sequence=0x{token:08X}")
            pong = await stage("pong_wait", wait_matching(
                pong_queue, lambda item: item["sequence"] == token,
                args.ping_timeout_s, f"binary PONG sequence 0x{token:08X}"),
                f"deadline_s={args.ping_timeout_s:g}")
            diagnostic("pong_preflight_passed", pong=pong)

        writer = cv2.VideoWriter(str(paths["video"]),
            cv2.VideoWriter_fourcc(*"mp4v"), fps, (width, height))
        if not writer.isOpened():
            raise RuntimeError("cannot create MP4 video writer using mp4v")
        sample_file = paths["samples"].open("x", newline="", encoding="utf-8")
        frame_file = paths["video_frames"].open("x", newline="", encoding="utf-8")
        sample_writer = csv.DictWriter(sample_file, fieldnames=CSV_HEADER)
        sample_writer.writeheader()
        frame_writer = csv.writer(frame_file)
        frame_writer.writerow(["frame_index", "host_monotonic_ns", "host_epoch_us"])

        start_tx = struct.pack("<BI", CMD_CAPTURE_START, token)
        write_started = time.time_ns()
        await stage("capture_start_write", client.write_gatt_char(
            RX_UUID, start_tx, response=True),
            f"tx_hex={start_tx.hex(' ').upper()} token=0x{token:08X}")
        write_returned = time.time_ns()
        print(f"[capture_start_write] returned_at={local_timestamp(write_returned)} "
              f"token=0x{token:08X} tx_hex={start_tx.hex(' ').upper()}",
              flush=True)
        diagnostic("capture_start_write_returned", token=token,
                   tx_hex=start_tx.hex(" ").upper(),
                   started_epoch_us=write_started // 1000,
                   returned_epoch_us=write_returned // 1000)
        before_wait = counters["samples_received"]
        try:
            start_ack = await stage("capture_start_wait_ack", wait_matching(
                control_queue, lambda item: item["subtype"] == 1 and
                item["session_token"] == token, args.control_timeout_s,
                f"Start ACK subtype=1 token=0x{token:08X}"),
                f"token=0x{token:08X} deadline_s={args.control_timeout_s:g}")
        except BaseException:
            during_wait = counters["samples_received"] - before_wait
            print(f"[capture_start_wait_ack] evidence samples_during_wait="
                  f"{during_wait} sample_total={counters['samples_received']} "
                  f"first_sequence={counters['first_sequence']!r} "
                  f"last_sequence={counters['last_sequence']!r}", flush=True)
            diagnostic("capture_start_wait_failed_evidence",
                       samples_during_wait=during_wait,
                       sample_total=counters["samples_received"],
                       first_sequence=counters["first_sequence"],
                       last_sequence=counters["last_sequence"])
            raise
        if start_ack["status"] != 0:
            raise RuntimeError(f"board rejected capture start: {start_ack!r}")

        capture_started, started_ns = True, time.time_ns()
        sample_writer.writerow(empty_row("capture_start", started_ns))
        writer.write(first_frame)
        frame_writer.writerow([0, time.monotonic_ns(), time.time_ns() // 1000])
        pending, expected, frame_index = None, None, 1

        def flush_pending(last):
            nonlocal pending
            if pending:
                pending["last_sample"] = int(last)
                sample_writer.writerow(pending)
                pending = None

        def process_samples():
            nonlocal pending, expected
            while not sample_queue.empty():
                sample = sample_queue.get_nowait()
                flush_pending(False)
                sequence = sample["sequence"]
                wanted = sequence if expected is None else expected
                delta = (sequence - wanted) & 0xFFFFFFFF
                if expected is None or delta == 0:
                    missing, reason = 0, ""
                elif delta < 0x80000000:
                    missing, reason = delta, "ble_sequence_gap"
                    sample_writer.writerow(empty_row("gap", sample["received_ns"],
                        device_sequence=sequence, expected_sequence=wanted,
                        missing_count=missing, loss_reason=reason))
                else:
                    missing, reason = 0, "duplicate_or_out_of_order"
                expected = (sequence + 1) & 0xFFFFFFFF
                x, y, z = sample["raw"]
                pending = empty_row("sample", sample["received_ns"],
                    device_sequence=sequence, expected_sequence=wanted,
                    missing_count=missing, device_uptime_ms=sample["uptime_ms"],
                    raw_x=x, raw_y=y, raw_z=z,
                    x_mg=f"{x * ACCEL_MG_PER_LSB:.3f}",
                    y_mg=f"{y * ACCEL_MG_PER_LSB:.3f}",
                    z_mg=f"{z * ACCEL_MG_PER_LSB:.3f}", loss_reason=reason)

        state["stage"] = "capture_recording"
        deadline = time.monotonic() + args.duration_s
        while time.monotonic() < deadline:
            if disconnect_event.is_set():
                sample_writer.writerow(empty_row("disconnect",
                                                  state["disconnected_ns"]))
                raise ConnectionError("BLE disconnected during capture; "
                                      "HCI reason unknown")
            ok, frame = camera.read()
            if not ok:
                errors.append("camera_read_failed")
                break
            writer.write(frame)
            frame_writer.writerow([frame_index, time.monotonic_ns(),
                                   time.time_ns() // 1000])
            frame_index += 1
            process_samples()
            await asyncio.sleep(0)

        stop_tx = struct.pack("<BI", CMD_CAPTURE_STOP, token)
        await stage("capture_stop", client.write_gatt_char(
            RX_UUID, stop_tx, response=True),
            f"tx_hex={stop_tx.hex(' ').upper()} token=0x{token:08X}")
        capture_started, stop_frames = False, {}
        stop_deadline = loop.time() + args.control_timeout_s
        while 5 not in stop_frames:
            control = await wait_matching(control_queue,
                lambda item: item["session_token"] == token,
                max(stop_deadline - loop.time(), 0.000001),
                "capture Stop control frames")
            if control["subtype"] in (2, 3, 4, 5):
                stop_frames[control["subtype"]] = control
        process_samples()
        flush_pending(True)
        ended_ns = time.time_ns()
        sample_writer.writerow(empty_row("capture_stop", ended_ns))
        if stop_frames[5]["status"]:
            errors.append(f"capture_stop_status_{stop_frames[5]['status']}")
        mappings = {2: ["generated", "enqueued", "capture_queue_full"],
                    3: ["notify_success", "notify_failure", "gap_samples"],
                    4: ["last_sequence", "fifo_recovery"]}
        for subtype, names in mappings.items():
            if subtype in stop_frames and len(stop_frames[subtype]["values"]) == len(names):
                board_stats.update(zip(names, stop_frames[subtype]["values"]))
        session_status = "complete"
    except BaseException as exc:
        primary_exc = exc
        errors.append(f"primary_error: {type(exc).__name__}: {exc!r}")
        diagnostic("primary_error", stage=state["stage"],
                   exception_type=type(exc).__name__, exception_repr=repr(exc),
                   windows_codes=exception_codes(exc))
    finally:
        ended_ns = ended_ns or time.time_ns()
        if capture_started and client is not None and client.is_connected:
            stop_tx = struct.pack("<BI", CMD_CAPTURE_STOP, token)
            await cleanup("capture_stop_cleanup", lambda: client.write_gatt_char(
                RX_UUID, stop_tx, response=True))
        if notify_started and client is not None:
            await cleanup("stop_notify", lambda: client.stop_notify(TX_UUID))
        if client is not None:
            await cleanup("disconnect", client.disconnect)
        if writer is not None:
            await cleanup("video_writer_release", writer.release)
        if camera is not None:
            await cleanup("camera_release", camera.release)
        if serial_task is not None:
            serial_task.cancel()
            await asyncio.gather(serial_task, return_exceptions=True)
        if serial_device is not None:
            await cleanup("serial_close", serial_device.close)
        for opened in (sample_file, frame_file, serial_text, serial_raw):
            if opened is not None:
                try:
                    opened.flush()
                    opened.close()
                except BaseException as exc:
                    secondary_errors.append({"step": "file_close",
                                             "type": type(exc).__name__,
                                             "repr": repr(exc)})
        # Finish callbacks already queued during cleanup before freezing metadata.
        await asyncio.sleep(0)
        if session_status == "complete" and secondary_errors:
            session_status = "incomplete"
        if args.ack_only_test and counters["samples_received"] != 0:
            errors.append("ack_only_test_received_unexpected_samples")
        if session_status == "complete" and errors:
            session_status = "incomplete"
        training_data_valid = (session_status == "complete"
                               and not args.ack_only_test
                               and counters["samples_received"] > 0)
        metadata = {
            "session_status": session_status,
            "training_data_valid": training_data_valid,
            "requested_test_mode": ("ack_only_diagnostic" if args.ack_only_test
                                    else "normal_capture"),
            "service_discovery_options": {"use_cached_services": False},
            "winrt_service_changed_retry": service_changed_retry,
            "training_data_review_required": True,
            "incomplete": session_status != "complete",
            "session_id": args.session_id, "session_token": token,
            "wearing": {"hand": args.hand,
                        "watch_face_direction": args.watch_face},
            "cli_arguments": {key: str(value) if isinstance(value, Path) else value
                              for key, value in vars(args).items()},
            "host_start_time": local_timestamp(started_ns) if started_ns else None,
            "host_end_time": local_timestamp(ended_ns),
            "ble_address": args.address,
            "disconnect": {"time": local_timestamp(state["disconnected_ns"])
                           if state["disconnected_ns"] else None,
                           "hci_reason": "unknown"
                           if state["disconnected_ns"] else None},
            "capture_protocol_version": PROTOCOL_VERSION,
            "board_notification_format": {
                "endianness": "little", "sample_size_bytes": 17,
                "fields": "frame_type:u8,version:u8,flags:u8,"
                "sample_sequence:u32,device_uptime_ms:u32,raw_x:i16,"
                "raw_y:i16,raw_z:i16", "accel_scale_mg_per_lsb": 0.122,
                "control_frames": "type=0x03, version, subtype, status, "
                "session_token, zero-to-three u32 values"},
            "host_notification_statistics": counters,
            "board_capture_statistics": board_stats,
            "script_version": SCRIPT_VERSION, "camera": camera_info,
            "synchronization": {"hardware_synchronized": False,
                "operator_guidance": "Remain still for 2 seconds after start, "
                "then perform three clear synchronization gestures/downbeats "
                "for manual alignment by sequence."},
            "errors": errors, "secondary_cleanup_errors": secondary_errors,
            "output_files": {key: value.name for key, value in paths.items()}}
        try:
            paths["metadata"].write_text(json.dumps(
                metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        except BaseException as exc:
            print(f"Metadata save secondary error: {exc!r}", file=sys.stderr)
        diagnostic("session_finalized", session_status=session_status,
                   secondary_error_count=len(secondary_errors))
        diag.close()
    if primary_exc is not None:
        raise primary_exc
    print(f"Capture {session_status}: {args.output_dir}")
    print("Board capture statistics: " + json.dumps(board_stats, sort_keys=True))


def main():
    require_packages()
    args = parse_args()
    try:
        asyncio.run(capture(args))
    except KeyboardInterrupt:
        print("Interrupted; cleanup requested.", file=sys.stderr)
        raise SystemExit(130)
    except Exception as exc:
        print("Capture failed with an unhandled exception.", file=sys.stderr)
        print(f"type(exc): {type(exc)!r}", file=sys.stderr)
        print(f"repr(exc): {exc!r}", file=sys.stderr)
        codes = exception_codes(exc)
        print(f"Windows HRESULT/WinError candidates: {codes!r}" if codes else
              "Windows HRESULT/WinError: unavailable", file=sys.stderr)
        print("Full traceback:", file=sys.stderr)
        traceback.print_exception(type(exc), exc, exc.__traceback__)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
