using System;
using System.Runtime.InteropServices;

// Use the exact same Windows QPC epoch as the local Python bridge.
// Unity's Mono Stopwatch can have a different epoch despite the same frequency.
public static class BteHostClock
{
    [DllImport("kernel32.dll")] private static extern bool QueryPerformanceCounter(out long value);
    [DllImport("kernel32.dll")] private static extern bool QueryPerformanceFrequency(out long value);
    public static long Frequency { get { if (!QueryPerformanceFrequency(out long f)) throw new InvalidOperationException("QPC frequency"); return f; } }
    public static long GetTimestamp() { if (!QueryPerformanceCounter(out long t)) throw new InvalidOperationException("QPC timestamp"); return t; }
}
