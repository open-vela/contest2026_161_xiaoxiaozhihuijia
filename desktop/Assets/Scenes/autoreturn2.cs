using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;

public class autoreturn2 : MonoBehaviour
{
    public float countTime = 0f;

    void Update()
    {
        CountTime2();
    }

    void CountTime2()
    {
        countTime += Time.deltaTime;
        if (countTime > 120.0f)
        {
            SceneManager.LoadScene(4);
        }
    }

}
