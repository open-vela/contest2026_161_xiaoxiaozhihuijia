using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace WristConductor.Core
{
    public class ScoreEngine
    {
        public event Action<int> OnComboChanged;
        public event Action<HitResult, int> OnNoteHit;
        public event Action<ScoreResult> OnSongComplete;

        private readonly List<MusicNote> _notes;
        private readonly Queue<MusicNote> _pendingNotes = new Queue<MusicNote>();

        private int _currentCombo = 0;
        private int _maxCombo = 0;
        private int _perfectCount = 0;
        private int _goodCount = 0;
        private int _missCount = 0;
        private int _totalScore = 0;
        private long _totalLatency = 0;
        private int _hitCount = 0;

        private float _currentToleranceWindow = 150f;
        private float _minToleranceWindow = 80f;
        private float _maxToleranceWindow = 250f;
        private bool _isActive = false;
        private long _songStartTime = 0;

        private const int PERFECT_SCORE = 100;
        private const int GOOD_SCORE = 50;
        private const int COMBO_BONUS_STEP = 50;

        private readonly List<float> _latencyHistory = new List<float>();
        private float _averageLatency = 0f;

        public bool IsActive => _isActive;
        public int CurrentCombo => _currentCombo;
        public int MaxCombo => _maxCombo;

        public ScoreEngine(List<MusicNote> notes)
        {
            _notes = notes.OrderBy(n => n.timestamp).ToList();
            foreach (var note in _notes) _pendingNotes.Enqueue(note.Clone());
        }

        public void Start(long songStartTime)
        {
            _isActive = true;
            _songStartTime = songStartTime;
            _currentCombo = 0;
            _maxCombo = 0;
            _perfectCount = 0;
            _goodCount = 0;
            _missCount = 0;
            _totalScore = 0;
            _totalLatency = 0;
            _hitCount = 0;
            _latencyHistory.Clear();
            _averageLatency = 0;
        }

        public void ProcessInput(ProcessedData input, long currentLocalTime)
        {
            if (!_isActive || _pendingNotes.Count == 0) return;

            var nextNote = _pendingNotes.Peek();
            long localNoteTime = _songStartTime + nextNote.timestamp;
            long timeDiff = currentLocalTime - localNoteTime;
            float timeDiffMs = Math.Abs(timeDiff);

            if (timeDiff > _currentToleranceWindow * 1.5f)
            {
                HandleMiss(nextNote);
                _pendingNotes.Dequeue();
                return;
            }

            if (input.gesture == nextNote.gestureType && input.gestureConfidence >= nextNote.minConfidence)
            {
                if (timeDiffMs <= _currentToleranceWindow)
                {
                    HitResult result = timeDiffMs / _currentToleranceWindow <= 0.3f ? 
                        HitResult.Perfect : HitResult.Good;
                    HandleHit(nextNote, result, timeDiff);
                    _pendingNotes.Dequeue();
                }
            }
        }

        private void HandleHit(MusicNote note, HitResult result, long latency)
        {
            _currentCombo++;
            if (_currentCombo > _maxCombo) _maxCombo = _currentCombo;
            OnComboChanged?.Invoke(_currentCombo);

            int baseScore = result == HitResult.Perfect ? PERFECT_SCORE : GOOD_SCORE;
            int comboBonus = (_currentCombo / COMBO_BONUS_STEP) * 10;
            float difficultyBonus = 1 + (note.difficultyWeight * 0.1f);
            int finalScore = (int)((baseScore + comboBonus) * difficultyBonus);

            _totalScore += finalScore;
            if (result == HitResult.Perfect) _perfectCount++;
            else _goodCount++;

            _totalLatency += Math.Abs(latency);
            _hitCount++;
            _averageLatency = _totalLatency / (float)_hitCount;

            OnNoteHit?.Invoke(result, finalScore);
        }

        private void HandleMiss(MusicNote note)
        {
            _currentCombo = 0;
            _missCount++;
            OnComboChanged?.Invoke(0);
            OnNoteHit?.Invoke(HitResult.Miss, 0);
        }

        public ScoreResult GetResult()
        {
            _isActive = false;
            while (_pendingNotes.Count > 0)
            {
                _pendingNotes.Dequeue();
                _missCount++;
            }

            var result = new ScoreResult(true)
            {
                totalCombo = _currentCombo,
                maxCombo = _maxCombo,
                perfectCount = _perfectCount,
                goodCount = _goodCount,
                missCount = _missCount,
                totalScore = _totalScore,
                averageLatency = _hitCount > 0 ? _totalLatency / _hitCount : 0,
                rating = CalculateRating()
            };

            int totalHits = _perfectCount + _goodCount;
            result.accuracy = _notes.Count > 0 ? totalHits / (float)_notes.Count : 0;

            OnSongComplete?.Invoke(result);
            return result;
        }

        private Rating CalculateRating()
        {
            if (_notes.Count == 0) return Rating.None;
            float perfectRate = _perfectCount / (float)_notes.Count;
            float scoreRate = _totalScore / (float)(_notes.Count * PERFECT_SCORE);

            if (scoreRate >= 0.98f && perfectRate >= 0.95f) return Rating.SSS;
            if (scoreRate >= 0.95f && perfectRate >= 0.85f) return Rating.SS;
            if (scoreRate >= 0.90f) return Rating.S;
            if (scoreRate >= 0.80f) return Rating.A;
            if (scoreRate >= 0.70f) return Rating.B;
            if (scoreRate >= 0.60f) return Rating.C;
            if (scoreRate >= 0.50f) return Rating.D;
            return Rating.None;
        }

        public (int notesRemaining, float tolerance, float avgLatency) GetStatistics()
        {
            return (_pendingNotes.Count, _currentToleranceWindow, _averageLatency);
        }
    }
}