using System;

using UnityEngine;
using System.Runtime.InteropServices;

namespace Planeverb {
    enum SourceDirectivityPattern {
        Omni,
        Cardioid
    }

    [RequireComponent(typeof(AudioSource))]
	[RequireComponent(typeof(PlaneverbEmitter))]
	class PVSpatialUploader : MonoBehaviour
	{
		private enum EffectData {
			SMOOTHING_FACTOR,
			WET_GAIN_RATIO,

			sourcePattern,
			dryGain,
			wetGain,
			rt60,
			lowPass,
			direcX,
			direcY,
			sDirectivityX,
			sDirectivityY,

			numParams
		};

		AudioSource source;
		PlaneverbEmitter emitter;

		// public interface
		public SourceDirectivityPattern sourcePattern;
		public float SMOOTHING = 2f;
		public float WET_GAIN_RATIO = 0.1f;

		void Start()
		{
			source = GetComponent<AudioSource>();
			emitter = GetComponent<PlaneverbEmitter>();

			source.SetSpatializerFloat((int)EffectData.SMOOTHING_FACTOR, SMOOTHING);
			source.SetSpatializerFloat((int)EffectData.WET_GAIN_RATIO, WET_GAIN_RATIO);
		}

		void Update()
		{

			PlaneverbDSPInput dspParams = emitter.GetAudioSource().GetInput();

			source.SetSpatializerFloat((int)EffectData.sourcePattern, (float)sourcePattern);
			source.SetSpatializerFloat((int)EffectData.dryGain, dspParams.obstructionGain);
			source.SetSpatializerFloat((int)EffectData.wetGain, dspParams.wetGain);
			source.SetSpatializerFloat((int)EffectData.rt60, dspParams.rt60);
			source.SetSpatializerFloat((int)EffectData.lowPass, dspParams.lowpass);
			source.SetSpatializerFloat((int)EffectData.direcX, dspParams.directionX);
			source.SetSpatializerFloat((int)EffectData.direcY, dspParams.directionY);
			source.SetSpatializerFloat((int)EffectData.sDirectivityX, dspParams.sourceDirectionX);
			source.SetSpatializerFloat((int)EffectData.sDirectivityY, dspParams.sourceDirectionY);
		}
	}
}