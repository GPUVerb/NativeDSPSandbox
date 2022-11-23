using UnityEngine;
using UnityEngine.SceneManagement;
using System.Collections;

public class DemoSelector : MonoBehaviour
{
    string[] demos =
    {
        "--- Reverbs and spatialization ---",
        "convolution reverb", "Colvolution reverb",
        "velvetreverb", "Velvet reverb",
        "spatialization", "HRTF spatializer (5.2)",
    };

    void OnGUI()
    {
        GUILayout.BeginHorizontal();
        int n = 0, i = 0;
        while (i < demos.Length)
        {
            bool header = demos[i][0] == '-';
            if (header || n++ == 4)
            {
                GUILayout.EndHorizontal();
                if (header)
                    GUILayout.Label(demos[i++]);
                GUILayout.BeginHorizontal();
                n = 0;
            }
            if (!header)
            {
                if (GUILayout.Button(demos[i + 1], GUILayout.Width(155)))
                    SceneManager.LoadScene(demos[i]);
                i += 2;
            }
        }
        GUILayout.EndHorizontal();
    }
}
