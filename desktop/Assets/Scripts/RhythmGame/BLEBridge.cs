using System;
using UnityEngine;
using WristConductor.Core;

public class BLEBridge : MonoBehaviour
{
    [Header("蓝牙插件")]
    [Tooltip("勾选使用 TCP 模拟器测试，取消勾选使用真实 BLE 插件")]
    [SerializeField] private bool useTcpSimulator = true;
    [SerializeField] private int tcpPort = 9877;
    [Tooltip("真实 BLE 插件组件（取消 useTcpSimulator 时使用）")]
    [SerializeField] private MonoBehaviour realBlePluginComponent;
    [SerializeField] private bool autoConnectOnStart = true;

    private WristConductorController _controller;
    private IBlePlugin _plugin;
    private bool _isConnected;

    public bool IsConnected => _isConnected;

    public event Action<bool> OnConnectionChanged;

    public void Initialize(WristConductorController controller)
    {
        _controller = controller;
    }

    void Start()
    {
        if (BteDesktopInput.Enabled)
        {
            Debug.Log("[BLEBridge] v28 desktop mode: legacy BLE/TCP simulator disabled");
            return;
        }
        if (useTcpSimulator)
        {
            _plugin = new TcpBleSimulator(tcpPort);
            Debug.Log($"[BLEBridge] 使用 TCP 模拟器，端口: {tcpPort}");
        }
        else
        {
            if (realBlePluginComponent is IBlePlugin plugin)
            {
                _plugin = plugin;
                Debug.Log("[BLEBridge] 使用真实 BLE 插件");
            }
            else if (realBlePluginComponent != null)
            {
                Debug.LogError("[BLEBridge] realBlePluginComponent 未实现 IBlePlugin 接口");
            }
            else
            {
                // 自动查找同 GameObject 上的 IBlePlugin 组件
                var plugins = GetComponents<IBlePlugin>();
                foreach (var p in plugins)
                {
                    if (p is MonoBehaviour mb && mb != this)
                    {
                        _plugin = p;
                        break;
                    }
                }

                if (_plugin == null)
                {
                    Debug.LogWarning("[BLEBridge] 未找到真实 BLE 插件，请添加 RealBlePlugin 组件");
                }
                else
                {
                    Debug.Log("[BLEBridge] 自动找到真实 BLE 插件");
                }
            }
        }

        if (_plugin != null)
        {
            _plugin.OnDataReceived += OnPluginDataReceived;
            _plugin.OnConnectionChanged += OnPluginConnectionChanged;
            _plugin.OnError += msg => Debug.LogWarning($"[BLE] {msg}");
        }

        if (autoConnectOnStart && _plugin != null)
        {
            _plugin.Begin();
        }
    }

    void OnDestroy()
    {
        if (_plugin != null)
        {
            _plugin.OnDataReceived -= OnPluginDataReceived;
            _plugin.OnConnectionChanged -= OnPluginConnectionChanged;
            _plugin.Stop();
        }
    }

    private void OnPluginDataReceived(byte[] rawData)
    {
        FeedBLEData(rawData);
    }

    private void OnPluginConnectionChanged(bool connected)
    {
        SetConnection(connected);
    }

    public void FeedBLEData(byte[] rawData)
    {
        if (BteDesktopInput.Enabled) return;
        if (_controller == null || !_isConnected) return;
        _controller.BLEManager.ProcessReceivedData(rawData);
    }

    public void SetConnection(bool connected)
    {
        if (_isConnected == connected) return;
        _isConnected = connected;

        if (_controller != null)
        {
            _controller.BLEManager.SetConnectionState(connected);
        }

        OnConnectionChanged?.Invoke(connected);
        Debug.Log(connected ? "手表已连接" : "手表已断开");
    }

    public void FeedGesture(GestureType gesture, float confidence = 1f,
        float accelX = 0, float accelY = 0, float accelZ = 0)
    {
        if (BteDesktopInput.Enabled) return;
        if (_controller == null || !_isConnected) return;

        ushort packetId = (ushort)Time.frameCount;
        long timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

        byte[] packet = BuildSimulatedPacket(packetId, timestamp, gesture, confidence,
            accelX, accelY, accelZ);
        _controller.BLEManager.ProcessReceivedData(packet);
    }

    private byte[] BuildSimulatedPacket(ushort packetId, long timestamp,
        GestureType gesture, float confidence,
        float ax, float ay, float az)
    {
        byte[] data = new byte[39];
        data[0] = 1;

        byte[] idBytes = BitConverter.GetBytes(packetId);
        Array.Copy(idBytes, 0, data, 1, 2);

        byte[] tsBytes = BitConverter.GetBytes(timestamp);
        Array.Copy(tsBytes, 0, data, 3, 8);

        data[11] = (byte)gesture;
        data[12] = (byte)(confidence * 255);
        data[13] = 0;

        Array.Copy(BitConverter.GetBytes(ax), 0, data, 14, 4);
        Array.Copy(BitConverter.GetBytes(ay), 0, data, 18, 4);
        Array.Copy(BitConverter.GetBytes(az), 0, data, 22, 4);

        byte[] zero = BitConverter.GetBytes(0f);
        Array.Copy(zero, 0, data, 26, 4);
        Array.Copy(zero, 0, data, 30, 4);
        Array.Copy(zero, 0, data, 34, 4);

        data[38] = 100;
        return data;
    }
}
