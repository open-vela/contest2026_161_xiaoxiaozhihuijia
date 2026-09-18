using UnityEngine;
using UnityEditor;

public class SongConfigCreator
{
    [MenuItem("RhythmGame/Create SongConfigs")]
    public static void CreateAllSongConfigs()
    {
        string folder = "Assets/Resources/Songs";
        if (!AssetDatabase.IsValidFolder("Assets/Resources"))
        {
            AssetDatabase.CreateFolder("Assets", "Resources");
        }
        if (!AssetDatabase.IsValidFolder(folder))
        {
            AssetDatabase.CreateFolder("Assets/Resources", "Songs");
        }

        CreateSong(folder, "小白船", 80, 120f, 3f, 1, 1,
            "Assets/杨烁 - 小白船.mp3");

        CreateSong(folder, "春天在哪里", 130, 110f, 3f, 2, 2,
            "Assets/杨烁 - 春天在哪里.mp3");

        AssetDatabase.Refresh();
        Debug.Log("两首歌曲配置已创建在 Assets/Resources/Songs/");
    }

    private static void CreateSong(string folder, string name, int bpm,
        float duration, float delay, int density, int index, string audioPath)
    {
        SongConfig config = ScriptableObject.CreateInstance<SongConfig>();
        config.songName = name;
        config.bpm = bpm;
        config.totalDuration = duration;
        config.startDelay = delay;
        config.noteDensity = density;
        config.songIndex = index;

        AudioClip clip = AssetDatabase.LoadAssetAtPath<AudioClip>(audioPath);
        if (clip != null)
        {
            config.audioClip = clip;
        }
        else
        {
            Debug.LogWarning($"未找到音频: {audioPath}，请手动关联");
        }

        string spritePath = "Assets/03-界面背景.png";
        Sprite sprite = AssetDatabase.LoadAssetAtPath<Sprite>(spritePath);
        if (sprite != null)
        {
            config.backgroundSprite = sprite;
        }

        AssetDatabase.CreateAsset(config, $"{folder}/{name}.asset");
        Debug.Log($"已创建歌曲配置: {name}");
    }
}
