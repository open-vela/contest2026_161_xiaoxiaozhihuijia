using UnityEngine;
using System.Collections.Generic;

public class NoteTrack : MonoBehaviour
{
    [Header("音符设置")]
    public GameObject notePrefab;
    public Transform noteContainer;
    public float scrollSpeed = 400f;
    public float judgmentLineY = 300f;
    public float spawnOffset = 600f;
    public float despawnBelow = -100f;

    [Header("音符样式")]
    public Color noteColor = new Color(0.1f, 0.3f, 0.7f, 1f);

    private List<NoteVisual> _notes = new List<NoteVisual>();
    private float _songStartTime;

    public struct NoteVisual
    {
        public GameObject obj;
        public RectTransform rect;
        public float targetTime;
        public bool judged;
    }

    public void SetupTrack(List<float> noteTimesSeconds, float songStartTime)
    {
        ClearTrack();
        _songStartTime = songStartTime;

        foreach (float t in noteTimesSeconds)
        {
            GameObject noteObj = Instantiate(notePrefab, noteContainer);
            noteObj.SetActive(false);

            var img = noteObj.GetComponent<UnityEngine.UI.Image>();
            if (img != null) img.color = noteColor;

            _notes.Add(new NoteVisual
            {
                obj = noteObj,
                rect = noteObj.GetComponent<RectTransform>(),
                targetTime = t,
                judged = false
            });
        }
    }

    public void UpdateTrack(float currentSongTime)
    {
        float lookAhead = spawnOffset / scrollSpeed;

        foreach (var note in _notes)
        {
            if (note.judged) continue;

            float timeUntilHit = note.targetTime - currentSongTime;

            if (timeUntilHit > lookAhead)
            {
                note.obj.SetActive(false);
                continue;
            }

            if (!note.obj.activeSelf) note.obj.SetActive(true);

            float yPos = judgmentLineY - timeUntilHit * scrollSpeed;
            note.rect.anchoredPosition = new Vector2(0, yPos);

            if (yPos < despawnBelow)
            {
                note.obj.SetActive(false);
            }
        }
    }

    public void MarkNearestNoteJudged(float currentSongTime, float windowSeconds)
    {
        float bestDist = float.MaxValue;
        int bestIdx = -1;

        for (int i = 0; i < _notes.Count; i++)
        {
            if (_notes[i].judged) continue;
            float dist = Mathf.Abs(_notes[i].targetTime - currentSongTime);
            if (dist < bestDist && dist <= windowSeconds)
            {
                bestDist = dist;
                bestIdx = i;
            }
        }

        if (bestIdx >= 0)
        {
            var n = _notes[bestIdx];
            n.judged = true;
            _notes[bestIdx] = n;
        }
    }

    public void ClearTrack()
    {
        foreach (var note in _notes)
        {
            if (note.obj != null) Destroy(note.obj);
        }
        _notes.Clear();
    }

    void OnDestroy()
    {
        ClearTrack();
    }
}
