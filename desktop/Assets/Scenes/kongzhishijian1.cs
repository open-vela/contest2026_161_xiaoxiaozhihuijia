using UnityEngine;
using UnityEngine.Video;

public class VideoAutoStop : MonoBehaviour
{
    [Tooltip("拖入物体上的VideoPlayer组件")]
    public VideoPlayer videoPlayer;

    [Tooltip("总共播放多少秒后停止")]
    public float totalPlayTime = 50f;

    private float timer;

    void Start()
    {
        timer = 0;
        // 开启循环（你Inspector已经勾选Loop也没关系，代码这里再确认一遍）
        videoPlayer.isLooping = true;
        videoPlayer.Play();
    }

    void Update()
    {
        if (!videoPlayer.isPlaying) return;

        // 累加真实播放时间
        timer += Time.deltaTime;

        if (timer >= totalPlayTime)
        {
            videoPlayer.Stop();
        }
    }
}
