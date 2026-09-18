using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using UnityEngine;
using UnityEngine.SceneManagement;
using Stopwatch = BteHostClock;

/// <summary>Desktop BTE1 transport. Only Update touches Unity/game objects.</summary>
public sealed class BteDesktopInput : MonoBehaviour
{
    public const string Version = "bte-desktop-2-qpc";
    public static BteDesktopInput Instance { get; private set; }
    public static bool Enabled => (Application.platform == RuntimePlatform.WindowsPlayer || Application.platform == RuntimePlatform.WindowsEditor)
        && !Array.Exists(Environment.GetCommandLineArgs(), a => a == "--legacy-input");
    public static bool TestInput;
    public static Func<BteBeat, string> HandleBeat;
    public string State { get; private set; } = "未连接";
    public long Accepted, Processed, Duplicate, Rejected, Dropped;
    public string LastResult = "等待真实板端事件";
    public string EvidencePath { get; private set; }
    private string source = "board_serial";
    private volatile bool running;
    private TcpListener listener;
    private TcpClient client;
    private Thread worker;
    private StreamWriter log;
    private readonly object gate = new object();
    private readonly Queue<Incoming> queue = new Queue<Incoming>();
    private readonly Queue<string> acks = new Queue<string>();
    private readonly Dictionary<string, uint> highest = new Dictionary<string, uint>();
    private int overflow;
    private long lastMessageTicks;
    private string latestSource = "board_serial";

    private struct Incoming { public string Text; public long Rx; }
    [Serializable] private sealed class Record
    {
        public string stage, source, raw, outcome, run_id, app_version = Version, scene;
        public uint session, event_id;
        public ulong beat_time_us, detected_time_us;
        public long host_rx_ticks, forward_ticks, unity_rx_ticks, unity_process_ticks, qpc_frequency;
    }

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
    private static void Boot()
    {
        if (!Enabled) return;
        HandleBeat = null;
        Instance = new GameObject("BTE1 Desktop Real Input").AddComponent<BteDesktopInput>();
        DontDestroyOnLoad(Instance.gameObject);
    }

    private void Awake()
    {
        Application.runInBackground = true;
        source = TestInput || Array.Exists(Environment.GetCommandLineArgs(), a => a == "--bte-test-input") ? "simulation" : "board_serial";
#if UNITY_EDITOR
        source = UnityEditor.SessionState.GetBool("BteSimulation", false) ? "simulation" : "board_serial";
#endif
        latestSource = source;
        string root = Path.GetFullPath(Path.Combine(Application.dataPath, "..", "BteIntegration", "evidence"));
        Directory.CreateDirectory(root);
        EvidencePath = Path.Combine(root, "unity_" + DateTime.UtcNow.ToString("yyyyMMdd_HHmmss_fff") + "_" + source + ".jsonl");
        log = new StreamWriter(EvidencePath, false, new UTF8Encoding(false)) { AutoFlush = true };
        Write("start", null, source + "; host serial receive time; original judgment windows");
        running = true;
        worker = new Thread(Listen) { IsBackground = true, Name = "BTE1 localhost input" };
        worker.Start();
    }

    private void Listen()
    {
        try
        {
            listener = new TcpListener(IPAddress.Loopback, source == "simulation" ? 9878 : 9877);
            listener.Start();
            while (running)
            {
                if (!listener.Pending()) { Thread.Sleep(20); continue; }
                client = listener.AcceptTcpClient();
                client.NoDelay = true;
                client.SendTimeout = 100;
                Enqueue("#connected", Stopwatch.GetTimestamp());
                var stream = client.GetStream();
                var buffer = new byte[8192];
                var text = new StringBuilder();
                try
                {
                    while (running)
                    {
                        if (client.Client.Poll(10000, SelectMode.SelectRead))
                        {
                            int n = stream.Read(buffer, 0, buffer.Length);
                            if (n == 0) break;
                            long rx = Stopwatch.GetTimestamp();
                            for (int i = 0; i < n; i++)
                            {
                                if (buffer[i] == 10) { Enqueue(text.ToString(), rx); text.Length = 0; }
                                else if (buffer[i] != 13) text.Append((char)buffer[i]);
                                if (text.Length > 8192) throw new IOException("Oversized wire line");
                            }
                        }
                        while (true)
                        {
                            string ack;
                            lock (gate) { if (acks.Count == 0) break; ack = acks.Dequeue(); }
                            byte[] data = Encoding.UTF8.GetBytes(ack + "\n");
                            stream.Write(data, 0, data.Length);
                        }
                    }
                }
                catch (Exception ex) { Enqueue("#error " + ex.Message, Stopwatch.GetTimestamp()); }
                finally
                {
                    client.Close();
                    lock (gate) acks.Clear();
                    Enqueue("#disconnected", Stopwatch.GetTimestamp());
                }
            }
        }
        catch (Exception ex) { if (running) Enqueue("#error " + ex.Message, Stopwatch.GetTimestamp()); }
    }

    private void Enqueue(string text, long ticks)
    {
        lock (gate)
        {
            if (queue.Count >= 256) { Interlocked.Increment(ref overflow); return; }
            queue.Enqueue(new Incoming { Text = text, Rx = ticks });
        }
    }

    private void Update()
    {
        int lost = Interlocked.Exchange(ref overflow, 0);
        if (lost > 0) { Dropped += lost; Write("queue_overflow", null, lost.ToString()); }
        for (int i = 0; i < 64; i++)
        {
            Incoming item;
            lock (gate) { if (queue.Count == 0) break; item = queue.Dequeue(); }
            if (item.Text.StartsWith("#"))
            {
                State = item.Text == "#connected" ? "TCP 已连接，等待串口状态" : "连接中断";
                Write("transport", null, item.Text);
                continue;
            }
            try
            {
                var m = JsonUtility.FromJson<BteMessage>(item.Text);
                if (m.schema != "bte_bridge_v1" || m.source != source) throw new FormatException("input source/schema mismatch");
                latestSource = m.source;
                lastMessageTicks = item.Rx;
                if (m.kind != "event")
                {
                    if (m.kind != "status" && m.kind != "ready") throw new FormatException("unknown message kind");
                    State = m.kind == "ready" || m.state == "board_ready" ? "板端 READY " + m.build_id :
                        m.state == "serial_open" ? "串口已打开，等待 READY" :
                        m.state == "receiving" ? "正在接收 " + m.build_id : "连接中断";
                    Write("status", null, State + " " + m.raw);
                    continue;
                }
                var beat = BteProtocol.Parse(m.raw);
                beat.Source = m.source; beat.RunId = m.run_id;
                beat.HostRxTicks = m.host_rx_ticks; beat.ForwardTicks = m.forward_ticks; beat.UnityRxTicks = item.Rx;
                Write("received", beat, "CRC valid");
                string key = m.source + ":" + beat.Session;
                if (highest.TryGetValue(key, out uint high) && beat.EventId <= high)
                {
                    Duplicate++; Write("duplicate_or_out_of_order", beat, "not scored"); continue;
                }
                if (highest.Count > 4096) throw new FormatException("session capacity; stop input");
                highest[key] = beat.EventId;
                double age = (Stopwatch.GetTimestamp() - beat.HostRxTicks) / (double)Stopwatch.Frequency;
                if (m.qpc_frequency != Stopwatch.Frequency || age < 0 || age > 2)
                {
                    Rejected++; Write("stale_or_clock_invalid", beat, age.ToString("F6", System.Globalization.CultureInfo.InvariantCulture)); continue;
                }
                Accepted++;
                State = "正在接收";
                LastResult = HandleBeat == null ? "ignored_no_gameplay" : HandleBeat(beat);
                Processed++;
                Write("processed", beat, LastResult);
            }
            catch (Exception ex)
            {
                Rejected++;
                Write("rejected", null, ex.Message + " " + item.Text);
            }
        }
        if (lastMessageTicks > 0 && (Stopwatch.GetTimestamp()-lastMessageTicks)/(double)Stopwatch.Frequency > 4)
            State = "连接中断（超过4秒无桥接状态）";
    }

    private void Write(string stage, BteBeat beat, string result)
    {
        var record = new Record { stage = stage, source = beat?.Source ?? latestSource, outcome = result,
            raw = beat?.Raw, run_id = beat?.RunId, session = beat?.Session ?? 0, event_id = beat?.EventId ?? 0,
            beat_time_us = beat?.BeatTimeUs ?? 0, detected_time_us = beat?.DetectedTimeUs ?? 0,
            host_rx_ticks = beat?.HostRxTicks ?? 0, forward_ticks = beat?.ForwardTicks ?? 0,
            unity_rx_ticks = beat?.UnityRxTicks ?? 0, unity_process_ticks = Stopwatch.GetTimestamp(),
            qpc_frequency = Stopwatch.Frequency, scene = SceneManager.GetActiveScene().path };
        string json = JsonUtility.ToJson(record);
        log.WriteLine(json);
        if (beat != null)
        {
            lock (gate) { if (acks.Count < 256) acks.Enqueue(json); }
        }
    }

    private void OnGUI()
    {
        var old = GUI.matrix;
        GUI.matrix = Matrix4x4.Scale(Vector3.one * Mathf.Max(1f, Screen.height / 900f));
        float width = Mathf.Min(550, Screen.width / Mathf.Max(1f, Screen.height / 900f) - 24);
        GUILayout.BeginArea(new Rect(12, 12, width, 245), GUI.skin.box);
        GUILayout.Label(source == "simulation" ? "SIMULATION TEST / 模拟测试，不算实机通过" : "小小指挥家 · v28 真实板端输入");
        GUILayout.Label("模式：通用拍点 / 主机串口接收时间（未精确同步）");
        GUILayout.Label(State);
        GUILayout.Label($"接受 {Accepted} / 处理 {Processed} / 重复 {Duplicate} / 拒绝 {Rejected} / 队列丢弃 {Dropped}");
        GUILayout.Label("最近反馈：" + LastResult);
        if (GUILayout.Button("重新开始原有拍点场景 scene9", GUILayout.Height(30)))
            SceneManager.LoadScene("scene9");
        GUILayout.Label("日志：" + Path.GetFileName(EvidencePath));
        GUILayout.EndArea();
        GUI.matrix = old;
    }

    private void OnDestroy()
    {
        running = false;
        try { client?.Close(); listener?.Stop(); } catch { }
        worker?.Join(1000);
        Write("stop", null, $"accepted={Accepted}, processed={Processed}, duplicate={Duplicate}, rejected={Rejected}, dropped={Dropped}");
        log?.Dispose();
        HandleBeat = null;
        Instance = null;
    }
}
