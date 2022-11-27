using System;

using UnityEngine;
using System.Runtime.InteropServices;

namespace Planeverb
{
    class PlaneverbUploader : MonoBehaviour
    {
        [DllImport("AudioPluginDemo")]
        private static extern bool uploadSignalAnalysis(float dryGain,
            float wetGain,
            float rt60,
            float lowPass,
            float direcX,
            float direcY,
            float sDirectX,
            float sDirectY);

        // a value on the range [0, 3), represents the index into the output fetcher array
        private PlaneverbAudioSource[] pvSources;
        private PlaneverbAudioSource[] audioThreadSources = new PlaneverbAudioSource[1024];

        private void Awake()
        {
            // Dry     myIndex = 0
            // ReverbA myIndex = 1
            // ReverbB myIndex = 2
            // ReverbC myIndex = 3
            /*			Debug.Assert(myIndex >= 0 && myIndex < MAX_REVERBS,
                            "PlaneverbReverb MyIndex not set properly!");*/

            // ensure must be attached to an object with an AudioSource component
            Debug.Assert(GetComponent<AudioSource>() != null,
                "PlaneverbReverb component attached to GameObject without an AudioSource!");
        }

        private void Update()
        {
            // update the sources array from the audio manager children list
            pvSources =
                PlaneverbAudioManager.
                pvDSPAudioManager.
                GetComponentsInChildren<PlaneverbAudioSource>();

            if (pvSources != null)
            {
                int numSources = pvSources.Length;
                for (int i = 0; i < pvSources.Length; ++i)
                {
                    audioThreadSources[i] = pvSources[i];
                }

                for (int i = 0; i < numSources; ++i)
                {
                    PlaneverbDSPInput dspParams = pvSources[i].GetInput();
                    /*					Debug.Log(dspParams.obstructionGain); */
                    uploadSignalAnalysis(
                        dspParams.obstructionGain, dspParams.wetGain,
                        dspParams.rt60, dspParams.lowpass,
                        dspParams.directionX, dspParams.directionY,
                        dspParams.sourceDirectionX, dspParams.sourceDirectionY);
                }
            }
        }
    }
}
