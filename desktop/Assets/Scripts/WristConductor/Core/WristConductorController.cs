using System;
using System.Collections.Generic;
using UnityEngine;

namespace WristConductor.Core
{
    public class WristConductorController : IDisposable
    {
        private BLECommunicationManager _bleManager;
        private LightweightTimeSync _timeSync;
        private AccelerometerProcessor _accelerometerProcessor;
        private ScoreEngine _scoreEngine;

        private bool _isRunning = false;

        public event Action<ProcessedData> OnDataProcessed;
        public event Action<ScoreResult> OnGameFinished;
        public event Action<Rating> OnRatingChanged;
        public event Action<int> OnComboChanged;

        public bool IsRunning => _isRunning;
        public BLECommunicationManager BLEManager => _bleManager;
        public LightweightTimeSync TimeSync => _timeSync;

        public WristConductorController()
        {
            _bleManager = new BLECommunicationManager();
            _timeSync = new LightweightTimeSync();
            _accelerometerProcessor = new AccelerometerProcessor();

            _bleManager.OnDataReceived += OnBLEDataReceived;
        }

        private void OnBLEDataReceived(WatchDataPacket packet)
        {
            try
            {
                long localTime = _timeSync.WatchToLocal(packet.timestamp);
                var processed = ProcessData(packet, localTime);
                
                if (_scoreEngine != null && _scoreEngine.IsActive)
                {
                    _scoreEngine.ProcessInput(processed, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());
                }

                OnDataProcessed?.Invoke(processed);
            }
            catch (Exception ex)
            {
                Debug.LogError($"处理数据错误: {ex.Message}");
            }
        }

        private ProcessedData ProcessData(WatchDataPacket packet, long localTime)
        {
            var (filteredX, filteredY, filteredZ, magnitude) =
                _accelerometerProcessor.Process(packet.accelerationX, packet.accelerationY, packet.accelerationZ);

            return new ProcessedData
            {
                timestamp = localTime,
                gesture = packet.gesture,
                filteredAccelX = filteredX,
                filteredAccelY = filteredY,
                filteredAccelZ = filteredZ,
                gestureConfidence = packet.confidence,
                movementMagnitude = magnitude,
                localTimeOffset = localTime - packet.timestamp
            };
        }

        public bool StartGame(List<MusicNote> notes, long songStartTime)
        {
            try
            {
                _scoreEngine = new ScoreEngine(notes);
                _scoreEngine.OnComboChanged += OnComboChangedInternal;
                _scoreEngine.OnSongComplete += OnSongCompleteInternal;
                _scoreEngine.Start(songStartTime);
                _isRunning = true;

                Debug.Log($"游戏开始，音符数: {notes.Count}");
                return true;
            }
            catch (Exception ex)
            {
                Debug.LogError($"启动游戏失败: {ex.Message}");
                return false;
            }
        }

        private void OnComboChangedInternal(int combo)
        {
            OnComboChanged?.Invoke(combo);
        }

        private void OnSongCompleteInternal(ScoreResult result)
        {
            _isRunning = false;
            OnRatingChanged?.Invoke(result.rating);
            OnGameFinished?.Invoke(result);
            Debug.Log($"游戏结束! 评级: {result.rating}, 得分: {result.totalScore}");
        }

        public void StopGame()
        {
            if (_scoreEngine != null && _scoreEngine.IsActive)
            {
                var result = _scoreEngine.GetResult();
                OnSongCompleteInternal(result);
            }
            _isRunning = false;
        }

        public void Reset()
        {
            StopGame();
            _accelerometerProcessor.Reset();
            _timeSync.Reset();
            _bleManager.SetConnectionState(false);
        }

        public void Dispose()
        {
            StopGame();
            if (_bleManager != null)
            {
                _bleManager.OnDataReceived -= OnBLEDataReceived;
            }
        }
    }
}