using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class SpatializerVisualizer : MonoBehaviour
{
    public GameObject prefab;

    // Start is called before the first frame update
    void Start()
    {
    }

    // Update is called once per frame
    void Update()
    {
        float[] spectrum = new float[128];
        AudioListener.GetSpectrumData(spectrum, 0, FFTWindow.BlackmanHarris);

        float val = 0;
        for (int i = 0; i < spectrum.Length; ++i)
        {
            val += spectrum[i];
        }
        val /= spectrum.Length;

        GameObject thing = Instantiate(prefab, new Vector3(-10 + Time.time, 3, 15), Quaternion.identity);
        thing.transform.localScale = new Vector3(0.01f, 5000f * val, 0.01f);
    }
}
