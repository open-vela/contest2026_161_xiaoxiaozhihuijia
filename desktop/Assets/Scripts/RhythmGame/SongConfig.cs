using UnityEngine;

[CreateAssetMenu(fileName = "NewSongConfig", menuName = "RhythmGame/SongConfig")]
public class SongConfig : ScriptableObject
{
    public string songName;
    public AudioClip audioClip;
    public int bpm = 120;
    public float totalDuration = 60f;
    public float startDelay = 3f;
    public Sprite backgroundSprite;
    public int noteDensity = 1;
    public int songIndex;
}
