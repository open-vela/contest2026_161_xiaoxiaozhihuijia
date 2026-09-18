using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;

public class autoreturn1 : MonoBehaviour
{
    public float countTime = 0f;

    void Update()
    {
        CountTime1();
    }

    void CountTime1()
    {
        countTime += Time.deltaTime;
        if(countTime > 55.0f)
        {
            SceneManager.LoadScene(3);
        }
    }

}
