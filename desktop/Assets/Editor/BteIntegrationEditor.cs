using System;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

// Local, project-scoped editor commands. No network or hardware access.
[InitializeOnLoad]
public static class BteIntegrationEditor
{
    private static string Root => Path.GetFullPath(Path.Combine(Application.dataPath, "../BteIntegration"));
    static BteIntegrationEditor() { EditorApplication.update += Poll; }

    [MenuItem("RhythmGame/v28/Run real board input")]
    public static void RunReal()
    {
        if (EditorApplication.isPlaying) return;
        BteDesktopInput.TestInput = false;
        SessionState.SetBool("BteSimulation", false);
        EditorApplication.isPlaying = true;
    }

    [MenuItem("RhythmGame/v28/Inspect current project")]
    public static void Inspect()
    {
        Directory.CreateDirectory(Root);
        var report = "Editor=" + Application.unityVersion + "\nScene=" + SceneManager.GetActiveScene().path +
            "\nPlaying=" + EditorApplication.isPlaying + "\n";
        foreach (var obj in UnityEngine.Object.FindObjectsOfType<MonoBehaviour>(true))
            report += obj.gameObject.name + ": " + obj.GetType().FullName + "\n";
        foreach (var audio in UnityEngine.Object.FindObjectsOfType<AudioSource>(true))
            report += "Audio=" + audio.name + ", clip=" + (audio.clip == null ? "null" : audio.clip.name) + ", playing=" + audio.isPlaying + "\n";
        report += "\nBuild scenes:\n" + string.Join("\n", EditorBuildSettings.scenes.Select(s => s.path));
        File.WriteAllText(Path.Combine(Root, "editor_inspection.txt"), report);
    }

    private static void Poll()
    {
        string file = Path.Combine(Root, "editor_command.txt");
        if (!File.Exists(file) || EditorApplication.isCompiling || EditorApplication.isUpdating) return;
        string command = File.ReadAllText(file).Trim();
        File.Move(file, Path.Combine(Root, "command_" + DateTime.UtcNow.ToString("yyyyMMdd_HHmmss_fff") + ".done"));
        try
        {
            switch (command)
            {
                case "inspect": Inspect(); break;
                case "capture":
                    if (!EditorApplication.isPlaying) throw new InvalidOperationException("Capture requires Play mode");
                    ScreenCapture.CaptureScreenshot(Path.Combine(Root, "evidence", "game_" + DateTime.UtcNow.ToString("yyyyMMdd_HHmmss_fff") + ".png"));
                    break;
                case "play-real": RunReal(); break;
                case "stop": EditorApplication.isPlaying = false; break;
                case "play-simulation":
                    if (EditorApplication.isPlaying) throw new InvalidOperationException("Stop before changing source");
                    BteDesktopInput.TestInput = true;
                    SessionState.SetBool("BteSimulation", true);
                    EditorApplication.isPlaying = true;
                    break;
                case "game-scene":
                    if (EditorApplication.isPlaying) SceneManager.LoadScene("scene9");
                    else if (!SceneManager.GetActiveScene().isDirty) EditorSceneManager.OpenScene("Assets/Scenes/scene9.scene");
                    else throw new InvalidOperationException("Unsaved scene; not replacing it");
                    break;
                default: throw new InvalidOperationException("Unknown local editor command");
            }
        }
        catch (Exception ex) { File.AppendAllText(Path.Combine(Root, "editor_errors.txt"), ex + "\n"); }
    }
}
