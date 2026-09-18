using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.SceneManagement;

public class kaishi2 : MonoBehaviour
{
    public SongConfig songConfig;

    public void Jump1()
    {
        if (songConfig != null)
        {
            SelectedSongInfo.CurrentSong = songConfig;
            SelectedSongInfo.SongIndex = 1;
        }
        SceneManager.LoadScene("scene9");
    }
}
