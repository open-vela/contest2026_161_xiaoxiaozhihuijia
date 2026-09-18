using UnityEngine;
using UnityEngine.UI;
using UnityEditor;
using UnityEditor.SceneManagement;
using TMPro;

public class RhythmSceneSetup
{
    [MenuItem("RhythmGame/Auto Setup Game Scenes")]
    public static void SetupAllGameScenes()
    {
        string[] scenes = {
            "Assets/Scenes/scene9.scene",
            "Assets/Scenes/scene10.scene"
        };
        string[] songNames = { "小白船", "春天在哪里" };

        for (int i = 0; i < scenes.Length; i++)
        {
            SetupOneScene(scenes[i], songNames[i], i + 1);
        }

        EditorSceneManager.SaveScene(EditorSceneManager.GetActiveScene());
        AssetDatabase.Refresh();
        Debug.Log("两个游戏场景自动配置完成！");
    }

    private static void SetupOneScene(string scenePath, string songName, int songIndex)
    {
        var scene = EditorSceneManager.OpenScene(scenePath);
        Debug.Log($"正在配置: {scenePath} ({songName})");

        Canvas canvas = FindOrCreateCanvas();
        SetupCanvas(canvas);

        GameObject bleBridgeObj = FindOrCreate("BLEBridge");
        BLEBridge bridge = GetOrAddComponent<BLEBridge>(bleBridgeObj);

        GameObject noteContainerObj = FindOrCreate("NoteContainer");
        noteContainerObj.transform.SetParent(canvas.transform, false);
        RectTransform noteRect = noteContainerObj.GetComponent<RectTransform>();
        noteRect.anchorMin = new Vector2(0.5f, 0);
        noteRect.anchorMax = new Vector2(0.5f, 0);
        noteRect.sizeDelta = new Vector2(200, 800);
        noteRect.anchoredPosition = new Vector2(0, 400);

        GameObject judgeLine = FindOrCreate("JudgmentLine");
        judgeLine.transform.SetParent(canvas.transform, false);
        RectTransform lineRect = judgeLine.GetComponent<RectTransform>();
        lineRect.anchorMin = new Vector2(0.3f, 0.5f);
        lineRect.anchorMax = new Vector2(0.7f, 0.5f);
        lineRect.sizeDelta = new Vector2(0, 4);
        lineRect.anchoredPosition = Vector2.zero;
        Image lineImg = GetOrAddComponent<Image>(judgeLine);
        lineImg.color = new Color(0.1f, 0.3f, 0.7f, 0.8f);

        GameObject controllerObj = FindOrCreate("RhythmGameController");
        RhythmGameController controller = GetOrAddComponent<RhythmGameController>(controllerObj);

        AudioSource audioSource = controllerObj.GetComponent<AudioSource>();
        if (audioSource == null) audioSource = controllerObj.AddComponent<AudioSource>();
        audioSource.playOnAwake = false;
        audioSource.loop = false;

        controller.bleBridge = bridge;
        controller.audioSource = audioSource;

        NoteTrack noteTrack = controllerObj.GetComponent<NoteTrack>();
        if (noteTrack == null) noteTrack = controllerObj.AddComponent<NoteTrack>();
        noteTrack.noteContainer = noteRect;
        noteTrack.judgmentLineY = 300f;
        noteTrack.scrollSpeed = 400f;
        noteTrack.noteColor = new Color(0.1f, 0.3f, 0.7f, 1f);
        controller.noteTrack = noteTrack;

        GameObject scoreObj = FindOrCreateText("ScoreText", canvas.transform);
        TMP_Text scoreTmp = scoreObj.GetComponent<TMP_Text>();
        scoreTmp.text = "得分: 0";
        scoreTmp.fontSize = 36;
        scoreTmp.color = Color.white;
        scoreTmp.alignment = TextAlignmentOptions.TopLeft;
        RectTransform scoreRect = scoreObj.GetComponent<RectTransform>();
        scoreRect.anchorMin = new Vector2(0, 1);
        scoreRect.anchorMax = new Vector2(0, 1);
        scoreRect.pivot = new Vector2(0, 1);
        scoreRect.anchoredPosition = new Vector2(30, -30);
        scoreRect.sizeDelta = new Vector2(400, 60);
        controller.scoreText = scoreTmp;

        GameObject comboObj = FindOrCreateText("ComboText", canvas.transform);
        TMP_Text comboTmp = comboObj.GetComponent<TMP_Text>();
        comboTmp.text = "";
        comboTmp.fontSize = 48;
        comboTmp.color = new Color(1f, 0.8f, 0.2f);
        comboTmp.alignment = TextAlignmentOptions.Top;
        RectTransform comboRect = comboObj.GetComponent<RectTransform>();
        comboRect.anchorMin = new Vector2(0.5f, 1);
        comboRect.anchorMax = new Vector2(0.5f, 1);
        comboRect.pivot = new Vector2(0.5f, 1);
        comboRect.anchoredPosition = new Vector2(0, -20);
        comboRect.sizeDelta = new Vector2(600, 80);
        controller.comboText = comboTmp;

        GameObject songNameObj = FindOrCreateText("SongNameText", canvas.transform);
        TMP_Text songNameTmp = songNameObj.GetComponent<TMP_Text>();
        songNameTmp.text = songName;
        songNameTmp.fontSize = 28;
        songNameTmp.color = new Color(1, 1, 1, 0.8f);
        songNameTmp.alignment = TextAlignmentOptions.TopRight;
        RectTransform songRect = songNameObj.GetComponent<RectTransform>();
        songRect.anchorMin = new Vector2(1, 1);
        songRect.anchorMax = new Vector2(1, 1);
        songRect.pivot = new Vector2(1, 1);
        songRect.anchoredPosition = new Vector2(-30, -30);
        songRect.sizeDelta = new Vector2(400, 50);
        controller.songNameText = songNameTmp;

        GameObject judgeCanvasObj = FindOrCreate("JudgeCanvas");
        Canvas judgeCanvas = judgeCanvasObj.GetComponent<Canvas>();
        if (judgeCanvas == null)
        {
            judgeCanvas = judgeCanvasObj.AddComponent<Canvas>();
            judgeCanvas.renderMode = RenderMode.ScreenSpaceOverlay;
            judgeCanvas.sortingOrder = 10;
        }
        if (judgeCanvasObj.GetComponent<CanvasScaler>() == null)
            judgeCanvasObj.AddComponent<CanvasScaler>();
        if (judgeCanvasObj.GetComponent<GraphicRaycaster>() == null)
            judgeCanvasObj.AddComponent<GraphicRaycaster>();

        GameObject judgePrefab = CreateJudgeTextPrefab();
        controller.judgeTextPrefab = judgePrefab;
        controller.judgeCanvas = judgeCanvasObj.transform;

        Camera cam = Camera.main;
        if (cam != null)
        {
            cam.clearFlags = CameraClearFlags.SolidColor;
            cam.backgroundColor = new Color(0.05f, 0.05f, 0.15f);
        }

        EditorSceneManager.SaveScene(scene);
        Debug.Log($"  ✓ {songName} 场景配置完成");
    }

    private static Canvas FindOrCreateCanvas()
    {
        Canvas existing = Object.FindFirstObjectByType<Canvas>();
        if (existing != null) return existing;

        GameObject canvasObj = new GameObject("MainCanvas");
        Canvas canvas = canvasObj.AddComponent<Canvas>();
        canvas.renderMode = RenderMode.ScreenSpaceOverlay;
        canvas.sortingOrder = 0;

        CanvasScaler scaler = canvasObj.AddComponent<CanvasScaler>();
        scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
        scaler.referenceResolution = new Vector2(1920, 1080);

        canvasObj.AddComponent<GraphicRaycaster>();
        return canvas;
    }

    private static void SetupCanvas(Canvas canvas)
    {
        CanvasScaler scaler = canvas.GetComponent<CanvasScaler>();
        if (scaler == null) scaler = canvas.gameObject.AddComponent<CanvasScaler>();
        scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
        scaler.referenceResolution = new Vector2(1920, 1080);

        if (canvas.GetComponent<GraphicRaycaster>() == null)
            canvas.gameObject.AddComponent<GraphicRaycaster>();
    }

    private static GameObject FindOrCreate(string name)
    {
        GameObject existing = GameObject.Find(name);
        if (existing != null) return existing;
        GameObject obj = new GameObject(name);
        return obj;
    }

    private static GameObject FindOrCreateText(string name, Transform parent)
    {
        Transform existing = parent.Find(name);
        if (existing != null) return existing.gameObject;

        GameObject obj = new GameObject(name);
        obj.transform.SetParent(parent, false);
        TMP_Text tmp = obj.AddComponent<TextMeshProUGUI>();
        tmp.font = AssetDatabase.LoadAssetAtPath<TMP_FontAsset>(
            "Assets/TextMesh Pro/Fonts/LiberationSans SDF.asset");
        if (tmp.font == null)
        {
            string[] guids = AssetDatabase.FindAssets("LiberationSans SDF t:TMP_FontAsset");
            if (guids.Length > 0)
            {
                string path = AssetDatabase.GUIDToAssetPath(guids[0]);
                tmp.font = AssetDatabase.LoadAssetAtPath<TMP_FontAsset>(path);
            }
        }
        return obj;
    }

    private static T GetOrAddComponent<T>(GameObject obj) where T : Component
    {
        T comp = obj.GetComponent<T>();
        if (comp == null) comp = obj.AddComponent<T>();
        return comp;
    }

    private static GameObject CreateJudgeTextPrefab()
    {
        string prefabPath = "Assets/Scripts/RhythmGame/JudgeTextPrefab.prefab";

        GameObject prefab = AssetDatabase.LoadAssetAtPath<GameObject>(prefabPath);
        if (prefab != null) return prefab;

        GameObject obj = new GameObject("JudgeText");
        TMP_Text tmp = obj.AddComponent<TextMeshProUGUI>();
        tmp.fontSize = 64;
        tmp.alignment = TextAlignmentOptions.Center;
        tmp.color = Color.white;

        string[] guids = AssetDatabase.FindAssets("LiberationSans SDF t:TMP_FontAsset");
        if (guids.Length > 0)
        {
            string path = AssetDatabase.GUIDToAssetPath(guids[0]);
            tmp.font = AssetDatabase.LoadAssetAtPath<TMP_FontAsset>(path);
        }

        RectTransform rect = obj.GetComponent<RectTransform>();
        rect.sizeDelta = new Vector2(300, 100);

        prefab = PrefabUtility.SaveAsPrefabAsset(obj, prefabPath);
        Object.DestroyImmediate(obj);
        return prefab;
    }
}
