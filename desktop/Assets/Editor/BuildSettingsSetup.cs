using UnityEditor;
using UnityEngine;

public class BuildSettingsSetup
{
    [MenuItem("RhythmGame/Setup Build Settings (Add All Scenes)")]
    public static void SetupBuildScenes()
    {
        string[] scenePaths = {
            "Assets/Scenes/SampleScene.scene",
            "Assets/Scenes/Scene1.scene",
            "Assets/Scenes/scene2.scene",
            "Assets/Scenes/scene3.scene",
            "Assets/Scenes/scene4.scene",
            "Assets/Scenes/scene5.scene",
            "Assets/Scenes/scene6.scene",
            "Assets/Scenes/scene7.scene",
            "Assets/Scenes/scene8.scene",
            "Assets/Scenes/scene9.scene",
            "Assets/Scenes/scene10.scene"
        };

        EditorBuildSettingsScene[] editors = new EditorBuildSettingsScene[scenePaths.Length];
        for (int i = 0; i < scenePaths.Length; i++)
        {
            editors[i] = new EditorBuildSettingsScene(scenePaths[i], true);
        }

        EditorBuildSettings.scenes = editors;
        Debug.Log($"已添加 {scenePaths.Length} 个场景到构建列表");
    }
}
