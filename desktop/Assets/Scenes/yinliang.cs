using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Audio;

public class yinliang : MonoBehaviour
{
    public AudioMixer m_audioMixer;

    public void J_SetMainVolume(float fvalue)
    {
        m_audioMixer.SetFloat("音量",fvalue);
    }
}
