using System;
using System.Globalization;

[Serializable]
public sealed class BteMessage
{
    public string schema, source, kind, raw, state, build_id, run_id;
    public long host_rx_ticks, forward_ticks, qpc_frequency;
}

public sealed class BteBeat
{
    public uint Session, EventId;
    public ulong BeatTimeUs, DetectedTimeUs;
    public byte Flags;
    public string Source, Raw, RunId;
    public long HostRxTicks, ForwardTicks, UnityRxTicks;
}

public static class BteProtocol
{
    public static BteBeat Parse(string line)
    {
        var fields = (line ?? "").TrimEnd('\r', '\n').Split(',');
        if (fields.Length != 4 || fields[0] != "@BTE1" || fields[1] != "28" || fields[2].Length != 56 || fields[3].Length != 4)
            throw new FormatException("BTE1 length/envelope");
        byte[] bytes = new byte[28];
        for (int i = 0; i < bytes.Length; i++)
            bytes[i] = byte.Parse(fields[2].Substring(i * 2, 2), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture);
        ushort crc = 0xffff;
        foreach (byte b in bytes)
        {
            crc ^= b;
            for (int i = 0; i < 8; i++) crc = (ushort)((crc & 1) != 0 ? (crc >> 1) ^ 0xa001 : crc >> 1);
        }
        if (crc != ushort.Parse(fields[3], NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture))
            throw new FormatException("BTE1 CRC");
        if (bytes[0] != 1 || bytes[1] != 1 || (bytes[2] & ~1) != 0 || bytes[3] != 0)
            throw new FormatException("BTE1 unsupported fields");
        return new BteBeat { Session = (uint)ReadLE(bytes, 4, 4), EventId = (uint)ReadLE(bytes, 8, 4),
            BeatTimeUs = ReadLE(bytes, 12, 8), DetectedTimeUs = ReadLE(bytes, 20, 8), Flags = bytes[2], Raw = line };
    }

    private static ulong ReadLE(byte[] bytes, int offset, int length)
    {
        ulong value = 0;
        for (int i = 0; i < length; i++) value |= (ulong)bytes[offset + i] << (8 * i);
        return value;
    }
}
