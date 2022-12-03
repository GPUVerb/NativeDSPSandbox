using System;

using UnityEngine;
using System.Runtime.InteropServices;

namespace Planeverb
{
	//public enum PlaneverbSourceDirectivityPattern
	//{
	//	Omni,
	//	Cardioid,

	//	Count
	//}

	[RequireComponent(typeof(AudioSource))]
	[RequireComponent(typeof(PlaneverbEmitter))]
	class PVSpatialUploader : MonoBehaviour
	{
		private enum EffectData {
			sourcePattern, // corresponds to the enum DirectivityPattern
						   //sourceX = 1, // technically can be obtained from given unity spatializer parameters
						   //sourceY = 2,
			sourceForwardX,
			sourceForwardY,
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
		// for the listener (eventually ahve listener upload data independently somewhere to a context ?)
		//public GameObject listener;

		// public interface
		public PlaneverbSourceDirectivityPattern sourcePattern;
		// TODO: exposed variables to route to spatializer

		float sourceX, sourceY, sourceForwardX, sourceForwardY;

		void Start()
		{
			source = GetComponent<AudioSource>();
			emitter = GetComponent<PlaneverbEmitter>();
		}

		void Update()
		{
			//sourceX = transform.position.x;
			//sourceY = transform.position.z;
			sourceForwardX = transform.forward.x;
			sourceForwardY = transform.forward.z;

			//listenerX = listener.transform.position.x;
			//listenerY = listener.transform.position.z;

			PlaneverbDSPInput dspParams = emitter.GetAudioSource().GetInput();

			//source.SetSpatializerFloat((int)EffectData.sourceX, sourceX);
			//source.SetSpatializerFloat((int)EffectData.sourceY, sourceY);
			source.SetSpatializerFloat((int)EffectData.sourceForwardX, sourceForwardX);
			source.SetSpatializerFloat((int)EffectData.sourceForwardY, sourceForwardY);
			source.SetSpatializerFloat((int)EffectData.dryGain, dspParams.obstructionGain);
			source.SetSpatializerFloat((int)EffectData.wetGain, dspParams.wetGain);
			source.SetSpatializerFloat((int)EffectData.rt60, dspParams.rt60);
			source.SetSpatializerFloat((int)EffectData.lowPass, 30f);
			source.SetSpatializerFloat((int)EffectData.direcX, dspParams.directionX);
			source.SetSpatializerFloat((int)EffectData.direcY, dspParams.directionY);
			source.SetSpatializerFloat((int)EffectData.sDirectivityX, dspParams.sourceDirectionX);
			source.SetSpatializerFloat((int)EffectData.sDirectivityY, dspParams.sourceDirectionY);
		}
	}
}