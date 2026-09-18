using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace WristConductor.Core
{
    public class BLECommunicationManager
    {
        public event Action<WatchDataPacket> OnDataReceived;
        public event Action<bool> OnConnectionChanged;
        public event Action<string> OnError;

        private readonly object _packetLock = new object();
        private readonly Queue<WatchDataPacket> _receiveQueue = new Queue<WatchDataPacket>();
        private readonly Dictionary<ushort, WatchDataPacket> _packetBuffer = new Dictionary<ushort, WatchDataPacket>();

        private bool _isConnected = false;
        private ushort _lastPacketId = 0;
        private int _packetLossCount = 0;
        private const int MAX_QUEUE_SIZE = 100;

        private const byte PROTOCOL_VERSION = 1;
        private const int HEADER_SIZE = 6;

        public bool IsConnected => _isConnected;

        public bool ParsePacket(byte[] rawData, out WatchDataPacket packet)
        {
            packet = new WatchDataPacket();

            if (rawData == null || rawData.Length < HEADER_SIZE)
            {
                OnError?.Invoke("数据包太短");
                return false;
            }

            try
            {
                byte version = rawData[0];
                if (version != PROTOCOL_VERSION)
                {
                    OnError?.Invoke($"不支持的协议版本: {version}");
                    return false;
                }

                packet.packetId = BitConverter.ToUInt16(rawData, 1);
                packet.timestamp = BitConverter.ToInt64(rawData, 3);
                packet.gesture = (GestureType)rawData[11];
                packet.confidence = rawData[12] / 255f;
                packet.flags = rawData[13];

                packet.accelerationX = BitConverter.ToSingle(rawData, 14);
                packet.accelerationY = BitConverter.ToSingle(rawData, 18);
                packet.accelerationZ = BitConverter.ToSingle(rawData, 22);
                packet.angularVelocityX = BitConverter.ToSingle(rawData, 26);
                packet.angularVelocityY = BitConverter.ToSingle(rawData, 30);
                packet.angularVelocityZ = BitConverter.ToSingle(rawData, 34);
                packet.batteryLevel = rawData[38];

                return true;
            }
            catch (Exception ex)
            {
                OnError?.Invoke($"解析错误: {ex.Message}");
                return false;
            }
        }

        public void ProcessReceivedData(byte[] rawData)
        {
            if (!ParsePacket(rawData, out var packet)) return;

            lock (_packetLock)
            {
                if (_packetBuffer.ContainsKey(packet.packetId)) return;

                _packetBuffer[packet.packetId] = packet;
                if (_packetBuffer.Count > 200)
                {
                    var keysToRemove = _packetBuffer.Keys.Where(k => k < packet.packetId - 100).ToList();
                    foreach (var key in keysToRemove) _packetBuffer.Remove(key);
                }

                _receiveQueue.Enqueue(packet);
                if (_receiveQueue.Count > MAX_QUEUE_SIZE) _receiveQueue.Dequeue();

                OnDataReceived?.Invoke(packet);
            }
        }

        public bool TryGetNextPacket(out WatchDataPacket packet)
        {
            lock (_packetLock)
            {
                if (_receiveQueue.Count > 0)
                {
                    packet = _receiveQueue.Dequeue();
                    return true;
                }
            }
            packet = default;
            return false;
        }

        public (int queueSize, int totalPackets, float lossRate) GetStatistics()
        {
            lock (_packetLock)
            {
                int totalPackets = _packetBuffer.Count + _receiveQueue.Count;
                float lossRate = totalPackets > 0 ? _packetLossCount / (float)totalPackets : 0;
                return (_receiveQueue.Count, totalPackets, lossRate);
            }
        }

        public void SetConnectionState(bool connected)
        {
            if (_isConnected != connected)
            {
                _isConnected = connected;
                OnConnectionChanged?.Invoke(connected);
                if (!connected)
                {
                    lock (_packetLock)
                    {
                        _receiveQueue.Clear();
                        _packetBuffer.Clear();
                        _packetLossCount = 0;
                    }
                }
            }
        }
    }
}