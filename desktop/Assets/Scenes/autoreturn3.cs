using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;

public class autoreturn3 : MonoBehaviour
{
    public float countTime = 0f;

    void Update()
    {
        CountTime3();
    }

    void CountTime3()
    {
        countTime += Time.deltaTime;
        if (countTime > 110.0f)
        {
            SceneManager.LoadScene(5);
        }
    }

}
