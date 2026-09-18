using System;
using System.Collections.Generic;
using System.Linq;

namespace WristConductor.Core
{
    public class SignalFilter
    {
        private float _kalmanQ = 0.01f;
        private float _kalmanR = 0.1f;
        private float _kalmanP = 1.0f;
        private float _kalmanX = 0.0f;

        private float _lowPassAlpha = 0.15f;
        private float _lowPassValue = 0f;

        private readonly Queue<float> _medianBuffer = new Queue<float>();
        private readonly int _medianWindowSize = 5;

        private float _threshold = 3.0f;
        private float _mean = 0f;
        private float _stdDev = 1f;
        private readonly Queue<float> _valueHistory = new Queue<float>();
        private readonly int _historySize = 50;

        public void SetParameters(float kalmanQ = 0.01f, float kalmanR = 0.1f, float lowPassAlpha = 0.15f)
        {
            _kalmanQ = kalmanQ;
            _kalmanR = kalmanR;
            _lowPassAlpha = lowPassAlpha;
        }

        public float Filter(float value)
        {
            UpdateStatistics(value);
            if (IsOutlier(value)) value = _kalmanX;

            float medianFiltered = MedianFilter(value);
            float kalmanFiltered = KalmanFilter(medianFiltered);
            return LowPassFilter(kalmanFiltered);
        }

        private float KalmanFilter(float measurement)
        {
            _kalmanP = _kalmanP + _kalmanQ;
            _kalmanK = _kalmanP / (_kalmanP + _kalmanR);
            float predicted = _kalmanX;
            _kalmanX = predicted + _kalmanK * (measurement - predicted);
            _kalmanP = (1 - _kalmanK) * _kalmanP;
            return _kalmanX;
        }
        private float _kalmanK = 0.0f;

        private float LowPassFilter(float value)
        {
            _lowPassValue = _lowPassAlpha * value + (1 - _lowPassAlpha) * _lowPassValue;
            return _lowPassValue;
        }

        private float MedianFilter(float value)
        {
            _medianBuffer.Enqueue(value);
            if (_medianBuffer.Count > _medianWindowSize) _medianBuffer.Dequeue();

            if (_medianBuffer.Count < 3) return value;

            var sorted = _medianBuffer.ToList();
            sorted.Sort();
            return sorted[sorted.Count / 2];
        }

        private void UpdateStatistics(float value)
        {
            _valueHistory.Enqueue(value);
            if (_valueHistory.Count > _historySize) _valueHistory.Dequeue();

            if (_valueHistory.Count >= 10)
            {
                _mean = _valueHistory.Average();
                _stdDev = (float)Math.Sqrt(_valueHistory.Select(v => Math.Pow(v - _mean, 2)).Average());
                if (_stdDev < 0.001f) _stdDev = 0.001f;
            }
        }

        private bool IsOutlier(float value)
        {
            if (_valueHistory.Count < 10) return false;
            return Math.Abs((value - _mean) / _stdDev) > _threshold;
        }

        public void Reset()
        {
            _kalmanX = 0;
            _kalmanP = 1;
            _lowPassValue = 0;
            _medianBuffer.Clear();
            _valueHistory.Clear();
        }
    }
}