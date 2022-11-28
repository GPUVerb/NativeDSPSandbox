using System;

using UnityEngine;

namespace Planeverb
{
	//public enum PlaneverbSourceDirectivityPattern
	//{
	//	Omni,
	//	Cardioid,

	//	Count
	//}

	[RequireComponent(typeof(AudioSource))]
	[RequireComponent(typeof(PlaneverbEmitter))] // ideally this should be a script that goes solo. however rn its built off of existing system
	class OverhauledPlaneverbEmitter : MonoBehaviour
	{
		PlaneverbEmitter emitter;
		// for the listener (eventually ahve listener upload data independently somewhere to a context ?)
		public GameObject listener;

		// public interface
		//[Range(-48f, 12f)]
		//public float Volume;
		//private float volumeGain;
		public PlaneverbSourceDirectivityPattern sourcePattern;

		//technically global but keep it here for now
		float listenerX;
		float listenerY;

		// per-source thing
		float sourceX;
		float sourceY;
		float sourceForwardX;
		float sourceForwardY;

		float curr_sourceX = 0;
		float curr_sourceY = 0;
		float curr_sourceForwardX = 0;
		float curr_sourceForwardY = 0;

		// Analyzer output
		PlaneverbDSPInput dspParams;
		/*		float dryGain;
				float wetGain;
				float rt60;
				float lowPass;
				float direcX;
				float direcY;
				float sDirectivityX;
				float sDirectivityY;*/
		PlaneverbDSPInput curr_dspParams;
/*		float curr_dryGain = 1.0f;
		float curr_wetGain = 1.0f;
		float curr_rt60 = 0.0f;
		float curr_lowPass;
		float curr_direcX = 0;
		float curr_direcY = 0;
		float curr_sDirectivityX = 0;
		float curr_sDirectivityY = 0;*/

		void Start()
		{
			emitter = GetComponent<PlaneverbEmitter>();
		}

		void Update()
		{
			sourceX = transform.position.x;
			sourceY = transform.position.z;
			sourceForwardX = transform.forward.x;
			sourceForwardY = transform.forward.z;

			listenerX = listener.transform.position.x;
			listenerY = listener.transform.position.z;

			dspParams = emitter.GetAudioSource().GetInput();
		}

		const int PV_DSP_MAX_CALLBACK_LENGTH = 4096;
		const int PV_DSP_CHANNEL_COUNT = 2;
		const float PV_DSP_PI = 3.141593f;
		const float PV_DSP_SQRT_2 = 1.4142136f;
		const float PV_DSP_INV_SQRT_2 = 1.0f / PV_DSP_SQRT_2;
		const float PV_DSP_MAX_AUDIBLE_FREQ = 20000.0f;
		const float PV_DSP_MIN_AUDIBLE_FREQ = 20.0f;
		const float PV_DSP_T_ER_1 = 0.5f;
		const float PV_DSP_T_ER_2 = 1.0f;
		const float PV_DSP_T_ER_3 = 3.0f;
		const float PV_DSP_MIN_DRY_GAIN = 0.01f;
		const float TSTAR = 0.1f;

		float FindGainA(float rt60, float wetGain)
		{
			if (rt60 > PV_DSP_T_ER_2)
			{
				return 0.0f;
			}
			else if (rt60 < PV_DSP_T_ER_1)
			{
				return 1.0f;
			}

			float gain = wetGain;
			float term1 = Mathf.Pow(10.0f, -3.0f * TSTAR / PV_DSP_T_ER_2);
			float term2 = Mathf.Pow(10.0f, -3.0f * TSTAR / rt60);
			float term3 = Mathf.Pow(10.0f, -3.0f * TSTAR / PV_DSP_T_ER_1);
			float a = gain * (term1 - term2) / (term1 - term3);
			return a;
		}

		float FindGainB(float rt60, float wetGain)
		{
			if (rt60 < PV_DSP_T_ER_1)
			{
				return 0.0f;
			}

			float gain = wetGain;
			float term2 = Mathf.Pow(10.0f, -3.0f * TSTAR / rt60);

			// case we want j + 1 instead of j
			if (rt60 > PV_DSP_T_ER_2)
			{
				float term1 = Mathf.Pow(10.0f, -3.0f * TSTAR / PV_DSP_T_ER_3);
				float term3 = Mathf.Pow(10.0f, -3.0f * TSTAR / PV_DSP_T_ER_2);
				float a = gain * (term1 - term2) / (term1 - term3);
				return a;
			}
			else
			{
				float term1 = Mathf.Pow(10.0f, -3.0f * TSTAR / PV_DSP_T_ER_2);
				float term3 = Mathf.Pow(10.0f, -3.0f * TSTAR / PV_DSP_T_ER_1);
				float a = gain * (term1 - term2) / (term1 - term3);
				return gain - a;
			}
		}

		float FindGainC(float rt60, float wetGain) {
			if (rt60 > PV_DSP_T_ER_3)
			{
				return 1.0f;
			}
			else if (rt60 < PV_DSP_T_ER_2)
			{
				return 0.0f;
			}

			float gain = wetGain;
			float term1 = Mathf.Pow(10.0f, -3.0f * TSTAR / PV_DSP_T_ER_3);
			float term2 = Mathf.Pow(10.0f, -3.0f * TSTAR / rt60);
			float term3 = Mathf.Pow(10.0f, -3.0f * TSTAR / PV_DSP_T_ER_2);
			float a = gain * (term1 - term2) / (term1 - term3);
			return gain - a;
		}

		float CardioidPattern(float direcX, float direcY, float forwardX, float forwardY) {
			float dotValue = direcX * forwardX + direcY * forwardY; // dot product
			float cardioid = (1.0f + dotValue) / 2.0f;
			return (cardioid > PV_DSP_MIN_DRY_GAIN) ? cardioid : PV_DSP_MIN_DRY_GAIN;
		}

		private void OnAudioFilterRead(float[] data, int channels) {
			int dataBufferLength = data.Length;
			int length = dataBufferLength / channels;
			if (dspParams.lowpass < PV_DSP_MIN_AUDIBLE_FREQ || dspParams.lowpass > PV_DSP_MAX_AUDIBLE_FREQ ||
				dspParams.obstructionGain <= 0f || (dspParams.directionX == 0f && dspParams.directionY == 0f)) {
				// play zero sound, as a debugging ref.
				for (int i = 0; i < dataBufferLength; ++i) { data[i] = 0f; }
				return;
			}

			// TODO: ? reconsider?
			float[] outbuffer = new float[dataBufferLength];


			float lerpFactor = 1f / ((float)(length) * 2f); // expose as "SMOOTHING FACTOR"
															// exit if input is invalid - shouldn't happen.

			// TODO: if use spatialization? - have a checkbox param? or consider using a spatialization plugin.

			// coagulate channels into mono-data at the first "length" slots of data.
			for (int i = 0; i < length; ++i) { // agnostic for stereo+ - w/ spatialization, might not be necesary?
				float val = 0;
				for (int j = 0; j < channels; ++j)
				{
					val += data[i * channels + j];
				}
				data[i] = (val) / channels;
			}

			// TODO: Low pass filter? or leave it up to user effects?

			// incorporate wet gains, writing per-channel wet sum into outbuffer
			float targetRevGainA = FindGainA(dspParams.rt60, dspParams.wetGain);
			float targetRevGainB = FindGainB(dspParams.rt60, dspParams.wetGain);
			float targetRevGainC = FindGainC(dspParams.rt60, dspParams.wetGain);
			float currRevGainA = FindGainA(curr_dspParams.rt60, curr_dspParams.wetGain);
			float currRevGainB = FindGainB(curr_dspParams.rt60, curr_dspParams.wetGain);
			float currRevGainC = FindGainC(curr_dspParams.rt60, curr_dspParams.wetGain);
			//float currGainSum = 0;
			for (int i = 0; i < length; ++i)
			{
				float valA = data[i] * currRevGainA * 0.1f; //TODO: expose this as wetgainratio
				float valB = data[i] * currRevGainB * 0.1f;
				float valC = data[i] * currRevGainC * 0.1f;
				for (int j = 0; j < channels; ++j)
				{
					outbuffer[i * channels + j] = valA + valB + valC;
				}
				currRevGainA = Mathf.Lerp(currRevGainA, targetRevGainA, lerpFactor);
				currRevGainB = Mathf.Lerp(currRevGainB, targetRevGainB, lerpFactor);
				currRevGainC = Mathf.Lerp(currRevGainC, targetRevGainC, lerpFactor);
			}
			// incorporate dry gains:
			// if (sourcePattern == DirectivityPattern::Omni)
			float sDirectivityGainCurrent = 1.0f;
			float sDirectivityGainTarget = 1.0f;
			if (sourcePattern == PlaneverbSourceDirectivityPattern.Cardioid)
			{
				sDirectivityGainCurrent = CardioidPattern(curr_dspParams.sourceDirectionX, curr_dspParams.sourceDirectionY,
					curr_sourceForwardX, curr_sourceForwardY);
				sDirectivityGainTarget = CardioidPattern(dspParams.sourceDirectionX, dspParams.sourceDirectionY,
					sourceForwardX, sourceForwardY);
			}

			float distX = listenerX - sourceX;
			float distY = listenerY - sourceY;
			float euclideanDistance = Mathf.Sqrt(distX * distX + distY * distY);
			euclideanDistance = (euclideanDistance < 1f) ? 1f : euclideanDistance;
			float targetDistanceAttenuation = 1f / euclideanDistance;
			/// ///
			distX = listenerX - curr_sourceX;
			distY = listenerY - curr_sourceY;
			euclideanDistance = Mathf.Sqrt(distX * distX + distY * distY);
			euclideanDistance = (euclideanDistance < 1f) ? 1f : euclideanDistance;
			float currentDistanceAttenuation = 1f / euclideanDistance;

			float currDryGain = curr_dspParams.obstructionGain;
			float targetDryGain = Mathf.Max(dspParams.obstructionGain, PV_DSP_MIN_DRY_GAIN);

			for (int i = 0; i < length; ++i)
			{
				float val = data[i] * currDryGain * sDirectivityGainCurrent * currentDistanceAttenuation;

				// TODO: if spatialization, this should reflect that
				for (int j = 0; j < channels; ++j)
				{
					outbuffer[i * channels + j] += val;
				}
				currDryGain = Mathf.Lerp(currDryGain, targetDryGain, lerpFactor);
				sDirectivityGainCurrent = Mathf.Lerp(sDirectivityGainCurrent, sDirectivityGainTarget, lerpFactor);
				currentDistanceAttenuation = Mathf.Lerp(currentDistanceAttenuation, targetDistanceAttenuation, lerpFactor);
			}

			// TODO: this can be simpler.
			curr_dspParams.obstructionGain = currDryGain;
			for (int i = 0; i < length; ++i)
			{
				curr_dspParams.directionX		= Mathf.Lerp(curr_dspParams.directionX,			dspParams.directionX, lerpFactor);
				curr_dspParams.directionY		= Mathf.Lerp(curr_dspParams.directionY,			dspParams.directionY, lerpFactor);
				curr_dspParams.wetGain			= Mathf.Lerp(curr_dspParams.wetGain,		dspParams.wetGain, lerpFactor);
				curr_dspParams.rt60				= Mathf.Lerp(curr_dspParams.rt60,			dspParams.rt60, lerpFactor);
				curr_sourceForwardX				= Mathf.Lerp(curr_sourceForwardX, sourceForwardX, lerpFactor);
				curr_sourceForwardY				= Mathf.Lerp(curr_sourceForwardY, sourceForwardY, lerpFactor);
				curr_dspParams.sourceDirectionX = Mathf.Lerp(curr_dspParams.sourceDirectionX,	dspParams.sourceDirectionX, lerpFactor);
				curr_dspParams.sourceDirectionY = Mathf.Lerp(curr_dspParams.sourceDirectionY,	dspParams.sourceDirectionY, lerpFactor);
				curr_sourceX					= Mathf.Lerp(curr_sourceX, sourceX, lerpFactor);
				curr_sourceY					= Mathf.Lerp(curr_sourceY, sourceY, lerpFactor);
			}
			Array.Copy(outbuffer, data, dataBufferLength);
		}
	}
}