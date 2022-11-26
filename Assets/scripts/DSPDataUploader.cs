using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using System.Runtime.InteropServices;

public class DSPDataUploader : MonoBehaviour
{
    [DllImport("AudioPluginDemo")]
    private static extern bool uploadSignalAnalysis(float dryGain,
        float wetGain,
        float rt60,
        float lowPass,
        float direcX,
        float direcY,
        float sDirectX,
        float sDirectY,
        float sX,
        float sY);

    [DllImport("AudioPluginDemo")]
    private static extern bool updateListenerPos(float x, float y);

    // Start is called before the first frame update
    void Start()
    {}

    // Update is called once per frame
    void Update()
    {
        updateListenerPos(0.0f, 0.0f);
        uploadSignalAnalysis(1.07f, 0.2f, 0.42f, 20.0f,
            0.0f, 1.0f,
            0.0f, -1.0f,
            -2.0f, 0.0f);
    }
}
