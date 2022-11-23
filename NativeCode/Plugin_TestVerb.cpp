#include "AudioPluginUtil.h"
#include <cmath>

namespace TestVerb
{

    enum Param
    {
        P_OBSGAIN, //this causes bug ????
        P_RT60,
        P_WETGAIN,
        P_LOWPASS,
        P_DIRECX, //these only useful for spatialization
        P_DIRECY, //these only useful for spatialization
        P_SDIRECX,
        P_SDIRECY,
        P_LISTENX, // these useful for dry gain.
        P_LISTENY,
        P_SOURCEX,
        P_SOURCEY,
        P_NUM
    };

    struct EffectData
    {
        float params[P_NUM];
    };

    int InternalRegisterEffectDefinition(UnityAudioEffectDefinition& definition)
    {
        //TODO: expose as upload data.
        int numparams = P_NUM;
        definition.paramdefs = new UnityAudioParameterDefinition[numparams];
        AudioPluginUtil::RegisterParameter(definition, "ObsGain", "", 0.0f, 10.0f, 1.047f, 1.0f, 1.0f, P_OBSGAIN, "Obstruction gain");
        AudioPluginUtil::RegisterParameter(definition, "RT60", "s", 0.0f, 1.0f, 0.42f, 1.0f, 1.0f, P_RT60, "Delay time in seconds");
        AudioPluginUtil::RegisterParameter(definition, "WetGain", "", -20.0f, 10.0f, 0.2f, 1.0f, 1.0f, P_WETGAIN, "Wet gain");
        AudioPluginUtil::RegisterParameter(definition, "Low pass", "Hz", 0.0f, 5.0f, 2.0f, 1.0f, 1.0f, P_LOWPASS, "Low pass");
        AudioPluginUtil::RegisterParameter(definition, "Direct. X", "", -1.0f, 1.0f, 0.f,
            1.0f, 1.0f, P_DIRECX, "Directivity x");
        AudioPluginUtil::RegisterParameter(definition, "Direct. Y", "", -1.0f, 1.0f, 1.f,
            1.0f, 1.0f, P_DIRECY, "Directivity y");
        AudioPluginUtil::RegisterParameter(definition, "SDirect. X", "", -1.0f, 1.0f, 0.0,
            1.0f, 1.0f, P_SDIRECX, "Source Directivity X");
        AudioPluginUtil::RegisterParameter(definition, "SDirect. Y", "", -1.0f, 1.0f, -1,
            1.0f, 1.0f, P_SDIRECY, "Source Directivity Y");
        AudioPluginUtil::RegisterParameter(definition, "Listen X", "", -5.0f, 5.0f, 0.0f,
            1.0f, 1.0f, P_LISTENX, "Listener X");
        AudioPluginUtil::RegisterParameter(definition, "Listen Y", "", -5.0f, 5.0f, 0.0f,
            1.0f, 1.0f, P_LISTENY, "Listener Y");
        AudioPluginUtil::RegisterParameter(definition, "Source X", "", -5.0f, 5.0f, -2.0f,
            1.0f, 1.0f, P_SOURCEX, "Source X");
        AudioPluginUtil::RegisterParameter(definition, "Source Y", "", -5.0f, 5.0f, 0.0f,
            1.0f, 1.0f, P_SOURCEY, "Source Y");
        return numparams;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK CreateCallback(UnityAudioEffectState* state)
    {
        EffectData* effectdata = new EffectData;
        memset(effectdata, 0, sizeof(EffectData));
        state->effectdata = effectdata;
        AudioPluginUtil::InitParametersFromDefinitions(InternalRegisterEffectDefinition, effectdata->params);
        return UNITY_AUDIODSP_OK;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK ReleaseCallback(UnityAudioEffectState* state)
    {
        EffectData* data = state->GetEffectData<EffectData>();
        delete data;
        return UNITY_AUDIODSP_OK;
    }

    const constexpr unsigned short PV_DSP_MAX_CALLBACK_LENGTH = 4096;
    const constexpr unsigned short PV_DSP_CHANNEL_COUNT = 2;
    const constexpr float PV_DSP_PI = 3.141593f;
    const constexpr float PV_DSP_SQRT_2 = 1.4142136f;
    const constexpr float PV_DSP_INV_SQRT_2 = 1.f / PV_DSP_SQRT_2;
    const constexpr float PV_DSP_MAX_AUDIBLE_FREQ = 20000.f;
    const constexpr float PV_DSP_MIN_AUDIBLE_FREQ = 20.f;
    const constexpr float PV_DSP_T_ER_1 = 0.5f;
    const constexpr float PV_DSP_T_ER_2 = 1.0f;
    const constexpr float PV_DSP_T_ER_3 = 3.0f;
    const constexpr float PV_DSP_MIN_DRY_GAIN = 0.01f;
    const constexpr float TSTAR = 0.1f;

    inline float FindGainA(float rt60, float wetGain)
    {
        if (rt60 > PV_DSP_T_ER_2)
        {
            return 0.f;
        }
        else if (rt60 < PV_DSP_T_ER_1)
        {
            return 1.f;
        }

        float gain = wetGain;
        float term1 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_2);
        float term2 = std::pow(10.f, -3.f * TSTAR / rt60);
        float term3 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_1);
        float a = gain * (term1 - term2) / (term1 - term3);
        return a;
    }

    inline float FindGainB(float rt60, float wetGain)
    {
        if (rt60 < PV_DSP_T_ER_1)
        {
            return 0.f;
        }

        float gain = wetGain;
        float term2 = std::pow(10.f, -3.f * TSTAR / rt60);

        // case we want j + 1 instead of j
        if (rt60 > PV_DSP_T_ER_2)
        {
            float term1 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_3);
            float term3 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_2);
            float a = gain * (term1 - term2) / (term1 - term3);
            return a;
        }
        else
        {
            float term1 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_2);
            float term3 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_1);
            float a = gain * (term1 - term2) / (term1 - term3);
            return gain - a;
        }
    }

    inline float FindGainC(float rt60, float wetGain)
    {
        if (rt60 > PV_DSP_T_ER_3)
        {
            return 1.f;
        }
        else if (rt60 < PV_DSP_T_ER_2)
        {
            return 0.f;
        }

        float gain = wetGain;
        float term1 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_3);
        float term2 = std::pow(10.f, -3.f * TSTAR / rt60);
        float term3 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_2);
        float a = gain * (term1 - term2) / (term1 - term3);
        return gain - a;
    }

    enum PlaneverbDSPSourceDirectivityPattern
    {
        pvd_Omni,			// omni pattern
        pvd_Cardioid,		// cardioid 
        // add more here
        pvd_SourceDirectivityPatternCount
    };

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK ProcessCallback(UnityAudioEffectState* state, float* inbuffer, float* outbuffer, unsigned int length, int inchannels, int outchannels)
    {
        EffectData* data = state->GetEffectData<EffectData>();
        // don't do anything if input is invalid
        // for now lo pass ignore
        // data->params[P_LOWPASS] < PV_DSP_MIN_AUDIBLE_FREQ || data->params[P_LOWPASS] > PV_DSP_MAX_AUDIBLE_FREQ ||
        // data->params[P_OBSGAIN] <= 0.f || 
        if (data->params[P_DIRECX] == 0.f && data->params[P_DIRECY] == 0.f) {
            memset(outbuffer, 0, length * outchannels * sizeof(float)); // play zero sound, as a debugging ref.
            //memcpy(outbuffer, inbuffer, length * outchannels * sizeof(float)); // dry sound
            return UNITY_AUDIODSP_ERR_UNSUPPORTED;
        }

        // length is the audio block size per channel, * channels? or is it alreaady for 1 channel? check the other plugin
        // it seems to be audio block size per channel for 1 channel

        // if use spatialization? - consider using a spatialization plugin.

        // determine lerp factor ?
        // float lerpFactor = 1.f / ((float)m_numFrames * (float)m_config.dspSmoothingFactor);
        float lerpFactor = 1.f / ((float)(length) * (float)2); // should be exposed as parameter

        // do everything in-place in inbuffer???
        float* inPtrMono = inbuffer; // basically coagulate inbuffer at the front as mono-data.
        const float* inputPtr = inbuffer;
        for (int i = 0; i < length; ++i)
        {
            // todo, make this agnostic beyond stereo - see below in wet gain incorporation
            float left = *inputPtr++;
            float right = *inputPtr++;
            *inPtrMono++ = (left + right) * 0.5f;
        }
        inPtrMono = inbuffer;

        // incorporate wet gains, writing per-channel into
        float revGainA = FindGainA(data->params[P_RT60], data->params[P_WETGAIN]);
        float revGainB = FindGainB(data->params[P_RT60], data->params[P_WETGAIN]);
        float revGainC = FindGainC(data->params[P_RT60], data->params[P_WETGAIN]);
        float currGainSum = 0;
        float* outPtr = outbuffer;
        for (int i = 0; i < length; ++i) {
            // TODO: lerp somewhere here....
            // check below. more robust.
            float val = *inPtrMono++ * (revGainA + revGainB + revGainC) * 0.9; // this one goes to outbuffer

            *outPtr = 0;
            for (int j = 0; j < outchannels; ++j) {
                *(outPtr++) = val;
            }
            //currGainSum = std::lerp()
        }

        // incorporate dry gains
        float currDryGain = data->params[P_OBSGAIN];
        float targetDryGain = currDryGain; // figure this out later
            // todo: determine directivity gains of the source for cardioid etc.
        float sDirectivityGainCurrent = 1.f; // omnidirectional
        float sDirectivityGainTarget = 1.f;

        float distX = data->params[P_LISTENX] - data->params[P_SOURCEX];
        float distY = data->params[P_LISTENY] - data->params[P_SOURCEY];
        float euclideanDistance = std::sqrt(distX * distX + distY * distY);
        euclideanDistance = (euclideanDistance < 1.f) ? 1.f : euclideanDistance;
        float targetDistanceAttenuation = 1.f / euclideanDistance;
        float currentDistanceAttenuation = targetDistanceAttenuation;

        inPtrMono = inbuffer;
        outPtr = outbuffer;
        for (int i = 0; i < length; ++i)
        {
            float val = *inPtrMono++ * currDryGain * sDirectivityGainCurrent * currentDistanceAttenuation;
            // out += in, for every in
            for (int j = 0; j < outchannels; ++j) {
                *(outPtr++) += val;
            }

            // currDryGain = std::lerp(currDryGain, targetDryGain, lerpFactor);
            // sDirectivityGainCurrent = std::lerp(sDirectivityGainCurrent, sDirectivityGainTarget, lerpFactor);
            // currentDistanceAttenuation = std::lerp(currentDistanceAttenuation, targetDistanceAttenuation, lerpFactor);
        }
        return UNITY_AUDIODSP_OK;
    }

    extern "C" UNITY_AUDIODSP_EXPORT_API bool uploadSignalAnalysis(float ....) {

    }


    // leave these alone
    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK SetFloatParameterCallback(UnityAudioEffectState* state, int index, float value)
    {
        EffectData* data = state->GetEffectData<EffectData>();
        if (index >= P_NUM)
            return UNITY_AUDIODSP_ERR_UNSUPPORTED;
        data->params[index] = value;
        return UNITY_AUDIODSP_OK;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK GetFloatParameterCallback(UnityAudioEffectState* state, int index, float* value, char* valuestr)
    {
        EffectData* data = state->GetEffectData<EffectData>();
        if (index >= P_NUM)
            return UNITY_AUDIODSP_ERR_UNSUPPORTED;
        if (value != NULL)
            *value = data->params[index];
        if (valuestr != NULL)
            valuestr[0] = 0;
        return UNITY_AUDIODSP_OK;
    }

    int UNITY_AUDIODSP_CALLBACK GetFloatBufferCallback(UnityAudioEffectState* state, const char* name, float* buffer, int numsamples)
    {
        return UNITY_AUDIODSP_OK;
    }
    // end leave alone
}
