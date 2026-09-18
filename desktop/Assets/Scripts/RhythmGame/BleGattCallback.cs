using System;
using UnityEngine;

public class BleGattCallback : AndroidJavaProxy
{
    private readonly Action<int, string> _onStateChange;
    private readonly Action<byte[]> _onDataReceived;
    private readonly Action<string> _onError;

    public BleGattCallback(
        Action<int, string> onStateChange,
        Action<byte[]> onDataReceived,
        Action<string> onError)
        : base("android.bluetooth.BluetoothGattCallback")
    {
        _onStateChange = onStateChange;
        _onDataReceived = onDataReceived;
        _onError = onError;
    }

    public void onConnectionStateChange(AndroidJavaObject gatt, int status, int newState)
    {
        _onStateChange?.Invoke(newState, null);
    }

    public void onServicesDiscovered(AndroidJavaObject gatt, int status)
    {
        _onStateChange?.Invoke(status == 0 ? 10 : 11, null);
    }

    public void onCharacteristicChanged(AndroidJavaObject gatt, AndroidJavaObject characteristic)
    {
        try
        {
            sbyte[] signed = characteristic.Call<sbyte[]>("getValue");
            if (signed != null)
            {
                byte[] data = new byte[signed.Length];
                Buffer.BlockCopy(signed, 0, data, 0, signed.Length);
                _onDataReceived?.Invoke(data);
            }
        }
        catch (Exception e)
        {
            _onError?.Invoke($"读取特征值失败: {e.Message}");
        }
    }

    public void onCharacteristicWrite(AndroidJavaObject gatt, AndroidJavaObject characteristic, int status)
    {
        if (status != 0)
            _onError?.Invoke($"写入特征值失败, status={status}");
    }

    public void onDescriptorWrite(AndroidJavaObject gatt, AndroidJavaObject descriptor, int status)
    {
        if (status != 0)
            _onError?.Invoke($"写入描述符失败, status={status}");
    }
}

public class BleScanCallback : AndroidJavaProxy
{
    private readonly Action<string, string> _onDeviceFound;
    private readonly Action<string> _onError;

    public BleScanCallback(Action<string, string> onDeviceFound, Action<string> onError)
        : base("android.bluetooth.le.ScanCallback")
    {
        _onDeviceFound = onDeviceFound;
        _onError = onError;
    }

    public void onScanResult(int callbackType, AndroidJavaObject result)
    {
        try
        {
            var device = result.Call<AndroidJavaObject>("getDevice");
            if (device != null)
            {
                string address = device.Call<string>("getAddress");
                string name = device.Call<string>("getName");
                _onDeviceFound?.Invoke(name ?? "", address);
            }
        }
        catch (Exception e)
        {
            _onError?.Invoke($"扫描回调异常: {e.Message}");
        }
    }
}
