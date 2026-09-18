using UnityEngine;
using TMPro;

public class GameManagerExample : MonoBehaviour
{
    [Header("UI References")]
    public TMP_Text scoreText;
    public TMP_Text comboText;
    public TMP_Text statusText;
    public TMP_Text ratingText;

    private int combo = 0;
    private int score = 0;
    private bool isRunning = false;

    void Start()
    {
        Debug.Log("🎮 游戏启动!");
        UpdateUI();
        isRunning = true;
        statusText.text = "按 空格键 模拟命中";
    }

    void Update()
    {
        if (!isRunning) return;

        if (Input.GetKeyDown(KeyCode.Space))
        {
            HitNote();
        }

        if (Input.GetKeyDown(KeyCode.R))
        {
            ResetGame();
        }
    }

    void HitNote()
    {
        combo++;
        score += 100 + (combo / 10) * 10;
        statusText.text = $"🎯 命中! 连击 x{combo}";
        UpdateUI();
    }

    void UpdateUI()
    {
        if (comboText != null) comboText.text = $"连击: {combo}";
        if (scoreText != null) scoreText.text = $"得分: {score}";
        if (ratingText != null) ratingText.text = $"评级: {GetRating()}";
    }

    string GetRating()
    {
        if (score >= 2000) return "SSS";
        if (score >= 1500) return "SS";
        if (score >= 1000) return "S";
        if (score >= 700) return "A";
        if (score >= 400) return "B";
        if (score >= 200) return "C";
        return "D";
    }

    void ResetGame()
    {
        combo = 0;
        score = 0;
        statusText.text = "🔄 已重置，按空格继续";
        UpdateUI();
    }
}