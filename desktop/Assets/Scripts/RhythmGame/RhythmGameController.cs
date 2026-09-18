using System;
using UnityEngine;
using UnityEngine.Audio;
using UnityEngine.UI;
using TMPro;
using System.Collections;
using System.Collections.Generic;
using WristConductor.Core;

public class RhythmGameController : MonoBehaviour
{
    [Header("手表输入")]
    public BLEBridge bleBridge;

    [Header("音频")]
    public AudioSource audioSource;
    public AudioMixer audioMixer;

    [Header("音符轨道")]
    public NoteTrack noteTrack;

    [Header("UI - 得分")]
    public TMP_Text scoreText;
    public TMP_Text comboText;
    public TMP_Text songNameText;

    [Header("UI - 判定")]
    public GameObject judgeTextPrefab;
    public Transform judgeCanvas;
    public float judgeMoveUpSpeed = 80f;
    public float judgeFadeTime = 1f;

    [Header("判定窗口(秒)")]
    public float perfectWindow = 0.15f;
    public float goodWindow = 0.3f;

    [Header("音符生成")]
    [Range(1, 4)] public int noteDensity = 1;
    public float noteToleranceMs = 150f;

    private WristConductorController _wristController;
    private SongConfig _currentSong;
    private List<MusicNote> _musicNotes;
    private List<float> _noteTimesSeconds;
    private float _songStartTime;
    private bool _isPlaying;

    private int _currentScore;
    private int _currentCombo;
    private int _nextNoteIndex;

    void Start()
    {
        _currentSong = SelectedSongInfo.CurrentSong;
        if (_currentSong == null || _currentSong.audioClip == null)
        {
            Debug.LogError("未选择歌曲，请从选歌界面进入");
            return;
        }

        if (scoreText != null) scoreText.text = "0";
        if (comboText != null) comboText.text = "";
        if (songNameText != null) songNameText.text = _currentSong.songName;

        InitWristController();
        InitAudio();

        _musicNotes = GenerateMusicNotes();
        _noteTimesSeconds = new List<float>();
        foreach (var note in _musicNotes)
        {
            _noteTimesSeconds.Add(note.timestamp / 1000f);
        }

        StartCoroutine(StartCountdown());
    }

    private void InitWristController()
    {
        _wristController = new WristConductorController();

        if (bleBridge != null)
        {
            bleBridge.Initialize(_wristController);
            bleBridge.SetConnection(true);
        }

        _wristController.OnDataProcessed += OnBleDataProcessed;
        _wristController.OnComboChanged += OnComboChanged;
        _wristController.OnGameFinished += OnGameFinished;
        _wristController.OnRatingChanged += OnRatingChanged;
    }

    private void InitAudio()
    {
        if (audioSource == null)
        {
            audioSource = gameObject.AddComponent<AudioSource>();
        }
        audioSource.clip = _currentSong.audioClip;
        audioSource.playOnAwake = false;
        audioSource.loop = false;
    }

    private IEnumerator StartCountdown()
    {
        float delay = _currentSong.startDelay;

        for (int i = 3; i >= 1; i--)
        {
            if (comboText != null)
            {
                comboText.text = i.ToString();
                comboText.fontSize = 72;
            }
            yield return new WaitForSeconds(1f);
        }

        if (comboText != null)
        {
            comboText.text = "GO!";
        }
        yield return new WaitForSeconds(0.5f);
        if (comboText != null) comboText.text = "";

        StartGameplay();
    }

    private void StartGameplay()
    {
        _songStartTime = Time.time;
        long unixStartMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

        audioSource.time = 0;
        audioSource.Play();

        if (noteTrack != null)
        {
            noteTrack.SetupTrack(_noteTimesSeconds, _songStartTime);
        }

        _wristController.StartGame(_musicNotes, unixStartMs);
        _isPlaying = true;
        _nextNoteIndex = 0;

        Debug.Log($"游戏开始: {_currentSong.songName}, 音符数: {_musicNotes.Count}");
    }

    void Update()
    {
        if (!_isPlaying) return;

        float currentSongTime = Time.time - _songStartTime;

        if (noteTrack != null)
        {
            noteTrack.UpdateTrack(currentSongTime);
        }

        if (currentSongTime >= _currentSong.totalDuration + 2f)
        {
            EndGame();
        }
    }

    private void OnBleDataProcessed(ProcessedData data)
    {
        if (!_isPlaying) return;
        if (data.gesture == GestureType.None) return;

        float currentSongTime = Time.time - _songStartTime;
        ShowGestureFeedback(data.gesture, data.gestureConfidence, currentSongTime);
    }

    private void ShowGestureFeedback(GestureType gesture, float confidence, float songTime)
    {
        if (_nextNoteIndex >= _musicNotes.Count) return;

        var note = _musicNotes[_nextNoteIndex];
        float noteTimeSec = note.timestamp / 1000f;
        float delta = Mathf.Abs(songTime - noteTimeSec);
        float windowSec = noteToleranceMs / 1000f;

        if (gesture != note.gestureType) return;
        if (delta > windowSec * 1.5f) return;

        string text;
        Color color;

        if (delta <= windowSec * 0.3f)
        {
            text = "Perfect";
            color = Color.green;
        }
        else if (delta <= windowSec)
        {
            text = "Good";
            color = Color.yellow;
        }
        else
        {
            text = "Miss";
            color = Color.red;
        }

        ShowJudge(text, color);
        _nextNoteIndex++;
    }

    private void OnComboChanged(int combo)
    {
        _currentCombo = combo;
        if (comboText != null)
        {
            comboText.fontSize = 36;
            comboText.text = combo > 0 ? $"{combo} COMBO" : "";
        }
    }

    private void OnGameFinished(ScoreResult result)
    {
        _isPlaying = false;
        Debug.Log($"游戏结束! 得分: {result.totalScore}, 评级: {result.rating}, " +
                  $"Perfect: {result.perfectCount}, Good: {result.goodCount}, Miss: {result.missCount}");

        if (comboText != null)
        {
            comboText.fontSize = 48;
            comboText.text = $"评级: {result.rating}\n得分: {result.totalScore}\n" +
                             $"Perfect: {result.perfectCount}  Good: {result.goodCount}  Miss: {result.missCount}";
        }
    }

    private void OnRatingChanged(Rating rating)
    {
        Debug.Log($"当前评级: {rating}");
    }

    public void ShowJudge(string text, Color color)
    {
        if (judgeTextPrefab == null || judgeCanvas == null) return;

        GameObject obj = Instantiate(judgeTextPrefab, judgeCanvas);
        var tmp = obj.GetComponent<TMP_Text>();
        if (tmp != null)
        {
            tmp.text = text;
            tmp.color = color;
        }
        var rect = obj.GetComponent<RectTransform>();
        rect.anchoredPosition = new Vector2(UnityEngine.Random.Range(-80, 80), UnityEngine.Random.Range(-30, 30));
        StartCoroutine(FloatAndFade(tmp, rect));
    }

    private IEnumerator FloatAndFade(TMP_Text text, RectTransform rect)
    {
        float timer = 0;
        Color c = text.color;
        while (timer < judgeFadeTime)
        {
            timer += Time.deltaTime;
            rect.anchoredPosition += Vector2.up * judgeMoveUpSpeed * Time.deltaTime;
            text.color = new Color(c.r, c.g, c.b, 1 - timer / judgeFadeTime);
            yield return null;
        }
        Destroy(text.gameObject);
    }

    private List<MusicNote> GenerateMusicNotes()
    {
        List<MusicNote> notes = new List<MusicNote>();
        float beatLength = 60f / _currentSong.bpm;
        int totalBeats = Mathf.FloorToInt(_currentSong.totalDuration / beatLength);
        int density = _currentSong.noteDensity > 0 ? _currentSong.noteDensity : noteDensity;

        for (int b = 0; b < totalBeats; b++)
        {
            bool shouldPlace = false;

            switch (density)
            {
                case 1: shouldPlace = (b % 2 == 0); break;
                case 2: shouldPlace = true; break;
                case 3: shouldPlace = (b % 2 == 0) || (b % 4 == 1); break;
                case 4: shouldPlace = true; break;
            }

            if (!shouldPlace) continue;

            float noteTimeSec = _currentSong.startDelay + b * beatLength;
            long noteTimeMs = (long)(noteTimeSec * 1000);

            GestureType gesture = GestureType.Tap;

            notes.Add(new MusicNote
            {
                timestamp = noteTimeMs,
                duration = 0,
                gestureType = gesture,
                minConfidence = 0.3f,
                difficultyWeight = 0,
                isLongNote = false,
                beatIndex = b,
                toleranceWindow = noteToleranceMs
            });
        }

        return notes;
    }

    private void EndGame()
    {
        if (!_isPlaying) return;
        _isPlaying = false;

        if (audioSource != null && audioSource.isPlaying)
        {
            audioSource.Stop();
        }

        _wristController?.StopGame();
    }

    void OnDestroy()
    {
        if (_wristController != null)
        {
            _wristController.OnDataProcessed -= OnBleDataProcessed;
            _wristController.OnComboChanged -= OnComboChanged;
            _wristController.OnGameFinished -= OnGameFinished;
            _wristController.OnRatingChanged -= OnRatingChanged;
            _wristController.Dispose();
        }
    }
}
