using System;
using UnityEngine;
using TMPro;
using System.Collections;
using System.Collections.Generic;
using WristConductor.Core;

public class BeatJudgeManager : MonoBehaviour
{
    [Header("2/4拍 设置")]
    public int BPM = 120;
    [Header("节拍总时长(秒)")]
    public float totalTime = 55f;

    [Header("判定窗口(秒)")]
    public float perfectTime = 0.15f;
    public float goodTime = 0.3f;

    [Header("UI弹出文字")]
    public GameObject judgeTextPrefab;
    public Transform canvasTrans;
    public float moveUpSpeed = 80f;
    public float fadeTime = 1f;

    [Header("准备延迟：运行后等待几秒才开始节拍")]
    public float startDelay = 2f;

    [Header("手表输入")]
    public BLEBridge bleBridge;

    [Header("UI - 得分")]
    public TMP_Text scoreText;
    public TMP_Text comboText;

    private float beatLength;
    private float songStartTime;
    private Queue<float> noteTimeQueue = new Queue<float>();

    private float targetNoteTime;
    private bool waitingNote;
    private bool judged;

    private WristConductorController _wristController;
    private int _score;
    private int _combo;
    private AudioSource _boardAudio;
    private int _boardNoteIndex;
    private double _boardFallbackStart;

    public int BoardScore => _score;
    public int BoardCombo => _combo;

    void Start()
    {
        beatLength = 60f / BPM;
        if (BteDesktopInput.Enabled)
        {
            _boardAudio = FindFirstObjectByType<AudioSource>();
            _boardFallbackStart = Time.realtimeSinceStartupAsDouble;
            _boardNoteIndex = 0;
            BteDesktopInput.HandleBeat = OnBoardBeat;
            if (scoreText != null) scoreText.text = "0";
            if (comboText != null) comboText.text = "";
            Debug.Log($"[BTE1] Original BeatJudgeManager connected. BPM={BPM}, perfect={perfectTime}, good={goodTime}, audio={_boardAudio != null}");
            return;
        }
        songStartTime = Time.time + startDelay;
        noteTimeQueue.Clear();

        float songDuration = totalTime;
        int totalBeats = Mathf.FloorToInt(songDuration / beatLength);

        for (int b = 0; b < totalBeats; b++)
        {
            float noteTime = songStartTime + b * beatLength;
            noteTimeQueue.Enqueue(noteTime);
        }

        waitingNote = false;
        judged = false;

        InitWristInput();

        if (scoreText != null) scoreText.text = "0";
        if (comboText != null) comboText.text = "";

        Debug.Log($"生成音符总数：{noteTimeQueue.Count}，BPM:{BPM}，单拍时长:{beatLength:F3}s");
    }

    private void InitWristInput()
    {
        _wristController = new WristConductorController();

        if (bleBridge != null)
        {
            bleBridge.Initialize(_wristController);
            bleBridge.SetConnection(true);
        }

        _wristController.OnComboChanged += (combo) =>
        {
            _combo = combo;
            if (comboText != null)
                comboText.text = combo > 0 ? $"{combo} COMBO" : "";
        };

        _wristController.OnDataProcessed += (data) =>
        {
            if (data.gesture != GestureType.None)
                OnBleGestureDetected(data.gesture, data.gestureConfidence);
        };

        List<MusicNote> notes = new List<MusicNote>();
        for (int b = 0; b < Mathf.FloorToInt(totalTime / beatLength); b++)
        {
            long noteTimeMs = (long)((startDelay + b * beatLength) * 1000);
            notes.Add(new MusicNote
            {
                timestamp = noteTimeMs,
                gestureType = GestureType.Tap,
                minConfidence = 0.3f,
                beatIndex = b,
                toleranceWindow = goodTime * 1000f
            });
        }

        long unixStartMs = System.DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        _wristController.StartGame(notes, unixStartMs);
    }

    void Update()
    {
        if (BteDesktopInput.Enabled) return;
        if (!waitingNote && noteTimeQueue.Count > 0)
        {
            targetNoteTime = noteTimeQueue.Dequeue();
            waitingNote = true;
            judged = false;
        }

        if (!waitingNote && noteTimeQueue.Count <= 0)
        {
            return;
        }

        if (waitingNote && !judged)
        {
            float windowStart = targetNoteTime - goodTime;
            float windowEnd = targetNoteTime + goodTime;

            if (Time.time > windowEnd)
            {
                PopJudgeText("Miss", Color.red);
                _combo = 0;
                if (comboText != null) comboText.text = "";
                judged = true;
                waitingNote = false;
            }
        }
    }

    public void OnBleGestureDetected(GestureType gesture, float confidence)
    {
        if (BteDesktopInput.Enabled) return;
        if (gesture != GestureType.Tap) return;
        JudgeHit();
    }

    void JudgeHit()
    {
        if (!waitingNote || judged) return;

        float windowStart = targetNoteTime - goodTime;
        float windowEnd = targetNoteTime + goodTime;

        if (Time.time < windowStart || Time.time > windowEnd)
        {
            return;
        }

        float delta = Mathf.Abs(Time.time - targetNoteTime);
        string res;
        Color col;

        if (delta <= perfectTime)
        {
            res = "Perfect";
            col = Color.green;
            _score += 100 + (_combo / 10) * 10;
        }
        else if (delta <= goodTime)
        {
            res = "Good";
            col = Color.yellow;
            _score += 50;
        }
        else
        {
            res = "Miss";
            col = Color.red;
        }

        _combo++;
        if (comboText != null) comboText.text = _combo > 0 ? $"{_combo} COMBO" : "";
        if (scoreText != null) scoreText.text = _score.ToString();

        PopJudgeText(res, col);
        judged = true;
        waitingNote = false;
    }

    void PopJudgeText(string text, Color color)
    {
        if (judgeTextPrefab == null || canvasTrans == null) return;
        GameObject newTextObj = Instantiate(judgeTextPrefab, canvasTrans);
        TMP_Text tmpText = newTextObj.GetComponent<TMP_Text>();
        if (tmpText != null)
        {
            tmpText.text = text;
            tmpText.color = color;
        }
        RectTransform rect = newTextObj.GetComponent<RectTransform>();
        rect.anchoredPosition = new Vector2(UnityEngine.Random.Range(-100, 100), UnityEngine.Random.Range(-50, 50));
        StartCoroutine(FloatAndFade(tmpText, rect));
    }

    IEnumerator FloatAndFade(TMP_Text tmpText, RectTransform rect)
    {
        float timer = 0;
        Color originColor = tmpText.color;
        while (timer < fadeTime)
        {
            timer += Time.deltaTime;
            rect.anchoredPosition += new Vector2(0, moveUpSpeed * Time.deltaTime);
            float alpha = 1 - timer / fadeTime;
            tmpText.color = new Color(originColor.r, originColor.g, originColor.b, alpha);
            yield return null;
        }
        Destroy(tmpText.gameObject);
    }

    void OnDestroy()
    {
        if (BteDesktopInput.HandleBeat == OnBoardBeat) BteDesktopInput.HandleBeat = null;
        _wristController?.Dispose();
    }

    // Explicit generic beat entry. No direction, confidence or IMU values invented.
    public string OnBoardBeat(BteBeat beat)
    {
        double age = (BteHostClock.GetTimestamp() - beat.HostRxTicks) /
            (double)BteHostClock.Frequency;
        double now = _boardAudio != null && _boardAudio.clip != null
            ? (double)_boardAudio.timeSamples / _boardAudio.clip.frequency
            : Time.realtimeSinceStartupAsDouble - _boardFallbackStart;
        double inputTime = now - age;
        if (_boardAudio != null && _boardAudio.clip != null && !_boardAudio.isPlaying)
            return "ignored_audio_not_playing";
        if (inputTime < 0 || inputTime > totalTime + startDelay)
            return "ignored_outside_song";
        int total = Mathf.FloorToInt(totalTime / beatLength);
        int skipped = 0;
        while (_boardNoteIndex < total && startDelay + _boardNoteIndex * beatLength + goodTime < inputTime)
        {
            _boardNoteIndex++;
            skipped++;
        }
        if (skipped > 0) _combo = 0;
        string result = "Miss";
        Color color = Color.red;
        double delta = _boardNoteIndex < total ? Math.Abs(inputTime - (startDelay + _boardNoteIndex * beatLength)) : double.MaxValue;
        if (delta <= goodTime)
        {
            if (delta <= perfectTime) { result = "Perfect"; color = Color.green; _score += 100 + (_combo / 10) * 10; }
            else { result = "Good"; color = Color.yellow; _score += 50; }
            _combo++;
            _boardNoteIndex++;
        }
        else _combo = 0;
        if (scoreText != null) scoreText.text = _score.ToString();
        if (comboText != null) comboText.text = _combo > 0 ? $"{_combo} COMBO" : "";
        PopJudgeText(result, color);
        return string.Format(System.Globalization.CultureInfo.InvariantCulture,
            "{0}; score={1}; combo={2}; song_s={3:F6}; delta_s={4:F6}; skipped={5}; clock={6}",
            result, _score, _combo, inputTime, delta, skipped, _boardAudio != null && _boardAudio.clip != null ? "audio_samples_host_rx" : "game_monotonic_host_rx");
    }
}
