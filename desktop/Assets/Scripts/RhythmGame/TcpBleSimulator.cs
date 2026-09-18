using System;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using UnityEngine;

public class TcpBleSimulator : IBlePlugin
{
    public bool IsConnected { get; private set; }

    public event Action<byte[]> OnDataReceived;
    public event Action<bool> OnConnectionChanged;
    public event Action<string> OnError;

    private TcpListener _listener;
    private TcpClient _client;
    private NetworkStream _stream;
    private Thread _listenThread;
    private volatile bool _running;
    private readonly int _port;
    private readonly byte _expectedPacketSize = 39;

    public TcpBleSimulator(int port = 9877)
    {
        _port = port;
    }

    public void Begin()
    {
        _running = true;
        _listenThread = new Thread(ListenLoop) { IsBackground = true };
        _listenThread.Start();
        Debug.Log($"[TcpBleSimulator] 监听端口 {_port}，等待手表模拟器连接...");
    }

    public void Stop()
    {
        _running = false;
        CloseClient();
        if (_listener != null)
        {
            try { _listener.Stop(); } catch { }
            _listener = null;
        }
        if (_listenThread != null && _listenThread.IsAlive)
        {
            _listenThread.Join(2000);
        }
        SetConnected(false);
    }

    private void ListenLoop()
    {
        try
        {
            _listener = new TcpListener(IPAddress.Any, _port);
            _listener.Start();

            while (_running)
            {
                if (!_listener.Pending())
                {
                    Thread.Sleep(100);
                    continue;
                }

                _client = _listener.AcceptTcpClient();
                _stream = _client.GetStream();
                SetConnected(true);
                Debug.Log("[TcpBleSimulator] 模拟器已连接");

                byte[] buffer = new byte[_expectedPacketSize];
                while (_running && _client.Connected)
                {
                    int offset = 0;
                    while (offset < _expectedPacketSize)
                    {
                        int read = _stream.Read(buffer, offset, _expectedPacketSize - offset);
                        if (read == 0) break;
                        offset += read;
                    }

                    if (offset == _expectedPacketSize)
                    {
                        byte[] packet = new byte[_expectedPacketSize];
                        Array.Copy(buffer, packet, _expectedPacketSize);
                        OnDataReceived?.Invoke(packet);
                    }
                }

                CloseClient();
                SetConnected(false);
                Debug.Log("[TcpBleSimulator] 模拟器已断开");
            }
        }
        catch (Exception ex)
        {
            if (_running) OnError?.Invoke($"TcpBleSimulator: {ex.Message}");
        }
    }

    private void CloseClient()
    {
        if (_stream != null) { try { _stream.Close(); } catch { } _stream = null; }
        if (_client != null) { try { _client.Close(); } catch { } _client = null; }
    }

    private void SetConnected(bool connected)
    {
        if (IsConnected != connected)
        {
            IsConnected = connected;
            OnConnectionChanged?.Invoke(connected);
        }
    }
}
