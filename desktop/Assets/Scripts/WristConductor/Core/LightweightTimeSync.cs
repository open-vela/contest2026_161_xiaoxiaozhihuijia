using System;
using System.Collections.Generic;
using System.Linq;

namespace WristConductor.Core
{
    /// <summary>
    /// 轻量级时间同步
    /// </summary>
    public class LightweightTimeSync
    {
        private const int MAX_SAMPLES = 32;
        private const float FILTER_ALPHA = 0.25f;
        private const int ROUNDTRIP_TIMEOUT_MS = 1000;

        private readonly Queue<TimeSyncSample> _samples = new Queue<TimeSyncSample>();
        private long _averageOffset = 0;
        private bool _isInitialized = false;
        private readonly object _syncLock = new object();

        public long CurrentTimeOffset => _averageOffset;
        public bool IsSynced => _isInitialized && _samples.Count >= 3;

        public struct TimeSyncSample
        {
            public long clientSendTime;
            public long serverReceiveTime;
            public long serverSendTime;
            public long clientReceiveTime;
            public long roundtripTime;
            public long offset;
            public long timestamp;

            public bool IsValid => roundtripTime > 0 && roundtripTime < ROUNDTRIP_TIMEOUT_MS * 2;
        }

        public TimeSyncSample CreateRequest()
        {
            return new TimeSyncSample
            {
                clientSendTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };
        }

        public bool ProcessResponse(TimeSyncSample request, long serverReceiveTime, long serverSendTime)
        {
            var now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

            var response = new TimeSyncSample
            {
                clientSendTime = request.clientSendTime,
                serverReceiveTime = serverReceiveTime,
                serverSendTime = serverSendTime,
                clientReceiveTime = now,
                timestamp = now
            };

            response.roundtripTime = response.clientReceiveTime - response.clientSendTime;
            response.offset = ((serverReceiveTime - request.clientSendTime) +
                              (serverSendTime - now)) / 2;

            if (!response.IsValid) return false;

            lock (_syncLock)
            {
                _samples.Enqueue(response);
                if (_samples.Count > MAX_SAMPLES) _samples.Dequeue();

                var validSamples = _samples.Where(s => s.IsValid).ToList();
                if (validSamples.Count < 3) return false;

                long totalOffset = 0;
                long totalWeight = 0;

                foreach (var sample in validSamples.OrderBy(s => s.roundtripTime).Take(MAX_SAMPLES / 2))
                {
                    long weight = Math.Max(1, ROUNDTRIP_TIMEOUT_MS - sample.roundtripTime);
                    totalOffset += sample.offset * weight;
                    totalWeight += weight;
                }

                if (totalWeight > 0)
                {
                    long newOffset = totalOffset / totalWeight;
                    _averageOffset = (long)(FILTER_ALPHA * newOffset + (1 - FILTER_ALPHA) * _averageOffset);
                    _isInitialized = true;
                    return true;
                }
            }

            return false;
        }

        public long WatchToLocal(long watchTime)
        {
            return _isInitialized ? watchTime + _averageOffset : watchTime;
        }

        public void Reset()
        {
            lock (_syncLock)
            {
                _samples.Clear();
                _averageOffset = 0;
                _isInitialized = false;
            }
        }
    }
}