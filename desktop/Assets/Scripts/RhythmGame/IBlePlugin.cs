using System;

public interface IBlePlugin
{
    bool IsConnected { get; }
    event Action<byte[]> OnDataReceived;
    event Action<bool> OnConnectionChanged;
    event Action<string> OnError;

    void Begin();
    void Stop();
}
