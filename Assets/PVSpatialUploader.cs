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
			SPATIALIZE,
			MUTE_DRY,

			SMOOTHING_FACTOR,
			WET_GAIN_RATIO,
			// TODO: something for toggling dry output (on/off)

			sourcePattern,
			dryGain,
			wetGain,
			rt60,
			lowPass, // TODO: implement here? or leave up to user effects?
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

		public bool SPATIALIZE = true;
		public bool SUPPRESS_DRY_SOUND = false;

		void Start()
		{
			source = GetComponent<AudioSource>();
			emitter = GetComponent<PlaneverbEmitter>();
		}

		void Update()
		{
			PlaneverbDSPInput dspParams = emitter.GetAudioSource().GetInput();

			source.SetSpatializerFloat((int)EffectData.SPATIALIZE, Convert.ToSingle(SPATIALIZE));
			source.SetSpatializerFloat((int)EffectData.MUTE_DRY, Convert.ToSingle(SUPPRESS_DRY_SOUND));
			source.SetSpatializerFloat((int)EffectData.SMOOTHING_FACTOR, SMOOTHING);
			source.SetSpatializerFloat((int)EffectData.WET_GAIN_RATIO, WET_GAIN_RATIO);
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