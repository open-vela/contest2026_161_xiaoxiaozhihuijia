"""
BLE 手表模拟器 - 通过 TCP 发送模拟手势数据包
用法: python ble_simulator.py [--tap] [--swipe-up] [--interval 0.5] [--count 10]

默认以 0.5 秒间隔发送 Tap 手势，共 10 次。
"""
import socket
import struct
import time
import argparse
import sys

HOST = "127.0.0.1"
PORT = 9877
PACKET_SIZE = 39

GESTURES = {
    "tap": 1,
    "swipe-up": 2,
    "swipe-down": 3,
    "swipe-left": 4,
    "swipe-right": 5,
    "long-press": 6,
    "flick": 7,
}


def build_packet(packet_id: int, gesture: int, confidence: float = 1.0) -> bytes:
    timestamp = int(time.time() * 1000)
    data = bytearray(PACKET_SIZE)
    data[0] = 1  # version
    struct.pack_into("<H", data, 1, packet_id)
    struct.pack_into("<q", data, 3, timestamp)
    data[11] = gesture
    data[12] = int(confidence * 255)
    data[13] = 0  # flags
    struct.pack_into("<f", data, 14, 0.0)   # accelX
    struct.pack_into("<f", data, 18, 2.0)   # accelY
    struct.pack_into("<f", data, 22, 0.0)   # accelZ
    struct.pack_into("<f", data, 26, 0.0)   # gyroX
    struct.pack_into("<f", data, 30, 0.0)   # gyroY
    struct.pack_into("<f", data, 34, 0.0)   # gyroZ
    data[38] = 100  # battery
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description="BLE 手表模拟器")
    parser.add_argument("--tap", action="store_true", help="发送 Tap 手势")
    parser.add_argument("--swipe-up", action="store_true", help="发送 SwipeUp 手势")
    parser.add_argument("--swipe-down", action="store_true", help="发送 SwipeDown 手势")
    parser.add_argument("--interval", type=float, default=0.5, help="发送间隔(秒)")
    parser.add_argument("--count", type=int, default=10, help="发送次数")
    args = parser.parse_args()

    if args.swipe_up:
        gesture = GESTURES["swipe-up"]
    elif args.swipe_down:
        gesture = GESTURES["swipe-down"]
    else:
        gesture = GESTURES["tap"]

    print(f"连接 {HOST}:{PORT} ...")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))
        print("已连接! 开始发送手势数据...")

        for i in range(args.count):
            packet = build_packet(i + 1, gesture)
            sock.sendall(packet)
            print(f"  [{i+1}/{args.count}] 发送 {gesture} (confidence=1.0)")
            if i < args.count - 1:
                time.sleep(args.interval)

        print("发送完成!")
        sock.close()
    except ConnectionRefusedError:
        print("连接失败: 请确保 Unity 编辑器正在运行且 TcpBleSimulator 已启动")
        sys.exit(1)
    except Exception as e:
        print(f"错误: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
