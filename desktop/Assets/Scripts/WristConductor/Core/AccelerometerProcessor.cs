using System;

namespace WristConductor.Core
{
    public class AccelerometerProcessor
    {
        private readonly SignalFilter _filterX = new SignalFilter();
        private readonly SignalFilter _filterY = new SignalFilter();
        private readonly SignalFilter _filterZ = new SignalFilter();

        private float _magnitudeFilter = 0f;
        private const float MAGNITUDE_ALPHA = 0.3f;

        public (float x, float y, float z, float magnitude) Process(float x, float y, float z)
        {
            float filteredX = _filterX.Filter(x);
            float filteredY = _filterY.Filter(y);
            float filteredZ = _filterZ.Filter(z);

            float rawMagnitude = (float)Math.Sqrt(
                filteredX * filteredX +
                filteredY * filteredY +
                filteredZ * filteredZ
            );

            _magnitudeFilter = MAGNITUDE_ALPHA * rawMagnitude + (1 - MAGNITUDE_ALPHA) * _magnitudeFilter;

            return (filteredX, filteredY, filteredZ, _magnitudeFilter);
        }

        public void Reset()
        {
            _filterX.Reset();
            _filterY.Reset();
            _filterZ.Reset();
            _magnitudeFilter = 0;
        }
    }
}