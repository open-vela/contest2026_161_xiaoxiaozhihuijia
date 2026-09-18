using System;
using System.Collections;
using System.Collections.Concurrent;
using UnityEngine;

public class RealBlePlugin : MonoBehaviour, IBlePlugin
{
    [Header("BLE 设备配置")]
    [Tooltip("手表蓝牙名称关键字，匹配到即连接")]
    [SerializeField] private string deviceName = "Mi Watch";
    [Tooltip("手表数据服务 UUID")]
    [SerializeField] private string serviceUuid = "0000fff0-0000-1000-8000-00805f9b34fb";
    [Tooltip("手表数据特征 UUID")]
    [SerializeField] private string characteristicUuid = "0000fff1-0000-1000-8000-00805f9b34fb";

    public bool IsConnected { get; private set; }

    public event Action<byte[]> OnDataReceived;
    public event Action<bool> OnConnectionChanged;
    public event Action<string> OnError;

    private AndroidJavaObject _bluetoothAdapter;
    private AndroidJavaObject _scanner;
    private AndroidJavaObject _gatt;
    private AndroidJavaObject _scanCallback;
    private AndroidJavaObject _gattCallback;

    private bool _scanning;
    private bool _initialized;

    private readonly ConcurrentQueue<Action> _mainThreadActions = new ConcurrentQueue<Action>();

    public void Begin()
    {
        if (!_initialized) InitializeBleSdk();
        StartScanning();
    }

    private void InitializeBleSdk()
    {
        if (_initialized) return;

#if UNITY_ANDROID && !UNITY_EDITOR
        try
        {
            using (var unityPlayer = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
            using (var activity = unityPlayer.GetStatic<AndroidJavaObject>("currentActivity"))
            using (var bluetoothManager = activity.Call<AndroidJavaObject>("getSystemService", "bluetooth"))
            {
                _bluetoothAdapter = bluetoothManager.Call<AndroidJavaObject>("getAdapter");
            }

            if (_bluetoothAdapter == null)
            {
                OnError?.Invoke("无法获取蓝牙适配器，请确认设备支持蓝牙");
                return;
            }

            bool enabled = _bluetoothAdapter.Call<bool>("isEnabled");
            if (!enabled)
            {
                Debug.LogWarning("[RealBlePlugin] 蓝牙未开启");
                OnError?.Invoke("蓝牙未开启，请在系统设置中打开蓝牙");
            }

            _gattCallback = new BleGattCallback(
                OnGattStateChange,
                OnGattDataReceived,
                msg => _mainThreadActions.Enqueue(() => OnError?.Invoke(msg))
            );

            _initialized = true;
            Debug.Log("[RealBlePlugin] Android BLE 初始化完成");
        }
        catch (Exception e)
        {
            OnError?.Invoke($"BLE 初始化失败: {e.Message}");
        }
#else
        Debug.LogWarning("[RealBlePlugin] 仅支持 Android 真机，编辑器中无法使用");
#endif
    }

    private void RequestPermissions()
    {
#if UNITY_ANDROID && !UNITY_EDITOR
        using (var unityPlayer = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
        using (var activity = unityPlayer.GetStatic<AndroidJavaObject>("currentActivity"))
        {
            int sdkInt = new AndroidJavaClass("android.os.Build$VERSION")
                .GetStatic<int>("SDK_INT");

            string[] permissions;
            if (sdkInt >= 31)
            {
                permissions = new string[]
                {
                    "android.permission.BLUETOOTH_SCAN",
                    "android.permission.BLUETOOTH_CONNECT",
                    "android.permission.ACCESS_FINE_LOCATION"
                };
            }
            else
            {
                permissions = new string[]
                {
                    "android.permission.ACCESS_FINE_LOCATION"
                };
            }

            activity.Call("requestPermissions",
                new object[] { permissions, 1001 });
        }
#endif
    }

    public void StartScanning()
    {
        if (_scanning) return;
        if (!_initialized) InitializeBleSdk();

#if UNITY_ANDROID && !UNITY_EDITOR
        RequestPermissions();

        StartCoroutine(DelayedStartScan());
#else
        Debug.LogWarning("[RealBlePlugin] 仅支持 Android 真机");
#endif
    }

    private IEnumerator DelayedStartScan()
    {
        yield return new WaitForSeconds(1f);

#if UNITY_ANDROID && !UNITY_EDITOR
        try
        {
            _scanCallback = new BleScanCallback(
                OnScanDeviceFound,
                msg => _mainThreadActions.Enqueue(() => OnError?.Invoke(msg))
            );

            _scanner = _bluetoothAdapter.Call<AndroidJavaObject>("getBluetoothLeScanner");
            if (_scanner == null)
            {
                OnError?.Invoke("蓝牙扫描器不可用，请确认蓝牙已开启");
                yield break;
            }

            _scanning = true;
            _scanner.Call("startScan", _scanCallback);
            Debug.Log($"[RealBlePlugin] 开始扫描: {deviceName}");

            StartCoroutine(ScanTimeout(15f));
        }
        catch (Exception e)
        {
            OnError?.Invoke($"开始扫描失败: {e.Message}");
        }
#endif
    }

    private void OnScanDeviceFound(string name, string address)
    {
        if (!string.IsNullOrEmpty(name) && name.Contains(deviceName))
        {
            _mainThreadActions.Enqueue(() => ConnectToDevice(address, name));
        }
    }

    private void ConnectToDevice(string address, string name)
    {
#if UNITY_ANDROID && !UNITY_EDITOR
        StopScanningInternal();
        Debug.Log($"[RealBlePlugin] 连接设备: {name} [{address}]");

        try
        {
            using (var unityPlayer = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
            using (var activity = unityPlayer.GetStatic<AndroidJavaObject>("currentActivity"))
            using (var device = _bluetoothAdapter.Call<AndroidJavaObject>("getRemoteDevice", address))
            {
                _gatt = device.Call<AndroidJavaObject>("connectGatt", activity, false, _gattCallback);
            }
        }
        catch (Exception e)
        {
            OnError?.Invoke($"连接失败: {e.Message}");
        }
#endif
    }

    private void OnGattStateChange(int newState, string info)
    {
        _mainThreadActions.Enqueue(() =>
        {
            switch (newState)
            {
                case 2: // STATE_CONNECTED
                    Debug.Log("[RealBlePlugin] GATT 已连接，等待发现服务...");
                    if (_gatt != null)
                        _gatt.Call<bool>("discoverServices");
                    break;

                case 0: // STATE_DISCONNECTED
                    Debug.Log("[RealBlePlugin] GATT 已断开");
                    SetConnected(false);
                    break;

                case 10: // services discovered
                    Debug.Log("[RealBlePlugin] 服务发现完成");
                    LogAllServices();
                    SubscribeToCharacteristic();
                    SetConnected(true);
                    break;

                case 11: // service discovery failed
                    OnError?.Invoke("服务发现失败");
                    break;
            }
        });
    }

    private void OnGattDataReceived(byte[] data)
    {
        _mainThreadActions.Enqueue(() =>
        {
            if (data != null && data.Length > 0)
            {
                OnDataReceived?.Invoke(data);
            }
        });
    }

    private void LogAllServices()
    {
#if UNITY_ANDROID && !UNITY_EDITOR
        if (_gatt == null) return;
        try
        {
            using (var services = _gatt.Call<AndroidJavaObject>("getServices"))
            {
                int count = services.Call<int>("size");
                Debug.Log($"[RealBlePlugin] 共发现 {count} 个服务:");
                for (int i = 0; i < count; i++)
                {
                    using (var service = services.Call<AndroidJavaObject>("get", i))
                    {
                        string uuid = service.Call<AndroidJavaObject>("getUuid").Call<string>("toString");
                        Debug.Log($"  服务[{i}]: {uuid}");

                        using (var chars = service.Call<AndroidJavaObject>("getCharacteristics"))
                        {
                            int charCount = chars.Call<int>("size");
                            for (int j = 0; j < charCount; j++)
                            {
                                using (var ch = chars.Call<AndroidJavaObject>("get", j))
                                {
                                    string chUuid = ch.Call<AndroidJavaObject>("getUuid").Call<string>("toString");
                                    int props = ch.Call<int>("getProperties");
                                    bool notify = (props & 0x10) != 0;
                                    bool write = (props & 0x08) != 0;
                                    bool read = (props & 0x02) != 0;
                                    string flags = $"{(read ? "R" : "-")}{(write ? "W" : "-")}{(notify ? "N" : "-")}";
                                    Debug.Log($"    特征[{j}]: {chUuid} [{flags}]");
                                }
                            }
                        }
                    }
                }
            }
        }
        catch (Exception e)
        {
            Debug.LogWarning($"[RealBlePlugin] 枚举服务失败: {e.Message}");
        }
#endif
    }

    private void SubscribeToCharacteristic()
    {
#if UNITY_ANDROID && !UNITY_EDITOR
        if (_gatt == null) return;

        try
        {
            using (var service = _gatt.Call<AndroidJavaObject>("getService", MakeUuid(serviceUuid)))
            {
                if (service == null)
                {
                    OnError?.Invoke($"未找到服务: {serviceUuid}");
                    return;
                }

                using (var characteristic = service.Call<AndroidJavaObject>("getCharacteristic", MakeUuid(characteristicUuid)))
                {
                    if (characteristic == null)
                    {
                        OnError?.Invoke($"未找到特征: {characteristicUuid}");
                        return;
                    }

                    _gatt.Call<bool>("setCharacteristicNotification", characteristic, true);

                    using (var descriptor = characteristic.Call<AndroidJavaObject>("getDescriptor",
                        MakeUuid("00002902-0000-1000-8000-00805f9b34fb")))
                    {
                        if (descriptor != null)
                        {
                            descriptor.Call<bool>("setValue", new byte[] { 0x01, 0x00 });
                            _gatt.Call<bool>("writeDescriptor", descriptor);
                        }
                    }
                }
            }
        }
        catch (Exception e)
        {
            OnError?.Invoke($"订阅通知失败: {e.Message}");
        }
#endif
    }

    private static AndroidJavaObject MakeUuid(string uuidStr)
    {
        using (var uuidClass = new AndroidJavaClass("java.util.UUID"))
        {
            return uuidClass.CallStatic<AndroidJavaObject>("fromString", uuidStr);
        }
    }

    private void StopScanningInternal()
    {
        _scanning = false;
#if UNITY_ANDROID && !UNITY_EDITOR
        if (_scanner != null && _scanCallback != null)
        {
            try { _scanner.Call("stopScan", _scanCallback); } catch { }
        }
#endif
    }

    public void Stop()
    {
        StopScanningInternal();
        Disconnect();

#if UNITY_ANDROID && !UNITY_EDITOR
        if (_gatt != null)
        {
            try { _gatt.Call("disconnect"); } catch { }
            try { _gatt.Call("close"); } catch { }
            _gatt = null;
        }
#endif

        while (_mainThreadActions.TryDequeue(out _)) { }
        Debug.Log("[RealBlePlugin] 已停止");
    }

    private void Disconnect()
    {
        if (!IsConnected) return;
        SetConnected(false);
        Debug.Log("[RealBlePlugin] 已断开连接");
    }

    void Update()
    {
        while (_mainThreadActions.TryDequeue(out var action))
        {
            try { action?.Invoke(); }
            catch (Exception e) { Debug.LogError($"[RealBlePlugin] 主线程回调异常: {e}"); }
        }
    }

    private void SetConnected(bool connected)
    {
        if (IsConnected != connected)
        {
            IsConnected = connected;
            OnConnectionChanged?.Invoke(connected);
        }
    }

    private IEnumerator ScanTimeout(float seconds)
    {
        yield return new WaitForSeconds(seconds);
        if (_scanning)
        {
            StopScanningInternal();
            OnError?.Invoke($"扫描超时，未找到设备 \"{deviceName}\"");
        }
    }

    void OnDestroy()
    {
        Stop();
    }
}
