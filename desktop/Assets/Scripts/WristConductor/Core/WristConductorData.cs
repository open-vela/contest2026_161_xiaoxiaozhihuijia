using System;
using System.Collections.Generic;

namespace WristConductor.Core
{
    #region 枚举定义

    /// <summary>
    /// 手势动作类型
    /// </summary>
    public enum GestureType
    {
        None = 0,
        Tap = 1,
        SwipeUp = 2,
        SwipeDown = 3,
        SwipeLeft = 4,
        SwipeRight = 5,
        LongPress = 6,
        Flick = 7
    }

    /// <summary>
    /// 命中结果
    /// </summary>
    public enum HitResult
    {
        Miss = 0,
        Good = 1,
        Perfect = 2
    }

    /// <summary>
    /// 评级
    /// </summary>
    public enum Rating
    {
        None = 0,
        D = 1,
        C = 2,
        B = 3,
        A = 4,
        S = 5,
        SS = 6,
        SSS = 7
    }

    #endregion

    #region 数据结构

    /// <summary>
    /// 从智能手表接收的原始数据包
    /// </summary>
    [Serializable]
    public struct WatchDataPacket
    {
        public long timestamp;
        public GestureType gesture;
        public float accelerationX;
        public float accelerationY;
        public float accelerationZ;
        public float angularVelocityX;
        public float angularVelocityY;
        public float angularVelocityZ;
        public float confidence;
        public byte batteryLevel;
        public ushort packetId;
        public byte flags;

        public bool IsValid => gesture != GestureType.None && confidence > 0.5f;
    }

    /// <summary>
    /// 预处理后的数据
    /// </summary>
    public struct ProcessedData
    {
        public long timestamp;
        public GestureType gesture;
        public float filteredAccelX;
        public float filteredAccelY;
        public float filteredAccelZ;
        public float gestureConfidence;
        public float movementMagnitude;
        public long localTimeOffset;
    }

    /// <summary>
    /// 乐谱音符
    /// </summary>
    [Serializable]
    public class MusicNote
    {
        public long timestamp;
        public long duration;
        public GestureType gestureType;
        public float minConfidence;
        public float difficultyWeight;
        public bool isLongNote;
        public int beatIndex;
        public float toleranceWindow;

        public MusicNote Clone()
        {
            return new MusicNote
            {
                timestamp = this.timestamp,
                duration = this.duration,
                gestureType = this.gestureType,
                minConfidence = this.minConfidence,
                difficultyWeight = this.difficultyWeight,
                isLongNote = this.isLongNote,
                beatIndex = this.beatIndex,
                toleranceWindow = this.toleranceWindow
            };
        }
    }

    /// <summary>
    /// 评分结果
    /// </summary>
    public struct ScoreResult
    {
        public int totalCombo;
        public int maxCombo;
        public int perfectCount;
        public int goodCount;
        public int missCount;
        public int totalScore;
        public float accuracy;
        public long averageLatency;
        public Rating rating;
        public Dictionary<int, float> noteLatencies;

        // 构造函数
        public ScoreResult(bool initialize)
        {
            totalCombo = 0;
            maxCombo = 0;
            perfectCount = 0;
            goodCount = 0;
            missCount = 0;
            totalScore = 0;
            accuracy = 0;
            averageLatency = 0;
            rating = Rating.None;
            noteLatencies = new Dictionary<int, float>();
        }
    }

    #endregion
}