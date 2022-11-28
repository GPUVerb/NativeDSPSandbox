using System;

using UnityEngine;
using System.Runtime.InteropServices;

namespace Planeverb
{
    class PlaneverbUploader : MonoBehaviour
    {
        [DllImport("AudioPluginDemo")]
        private static extern bool uploadSignalAnalysis(
            int id,
            float dryGain,
            float wetGain,
            float rt60,
            float lowPass,
            float direcX,
            float direcY,
            float sDirectX,
            float sDirectY);

        private PlaneverbAudioSource[] pvSources;

        private void Awake()
        {}

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
                for (int i = 0; i < numSources; ++i)
                {
                    PlaneverbDSPInput dspParams = pvSources[i].GetInput();
                    /*					Debug.Log(dspParams.obstructionGain); */
                    // pvSources[i].GetEmissionID()

                    // in planeverbevrb, a separate audio buffer is sent each time
                    // but m_wet etc. looks like its being overwritten - but that cant be the case
                    uploadSignalAnalysis(
                        pvSources[i].GetEmissionGPVerbID(),
                        dspParams.obstructionGain, dspParams.wetGain,
                        dspParams.rt60, dspParams.lowpass,
                        dspParams.directionX, dspParams.directionY,
                        dspParams.sourceDirectionX, dspParams.sourceDirectionY);
                }
            }
        }
    }
}
