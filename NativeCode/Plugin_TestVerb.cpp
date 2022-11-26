#include "AudioPluginUtil.h"
#include <cmath>
#include <algorithm>

namespace TestVerb
{
    struct ReverbParams
    {
        float dryGain;
        float wetGain;
        float rt60;
        float lowPass;
        float direcX;
        float direcY;
        float sDirectX;
        float sDirectY;
        float sX;
        float sY;

        // prevs for interps
        float curr_dryGain = 1.f;
        float curr_wetGain = 1.f;
        float curr_rt60 = 0.f;
        float curr_lowPass;
        float curr_direcX = 0;
        float curr_direcY = 0;
        float curr_sDirectX = 0;
        float curr_sDirectY = 0;
        float curr_sX = 0;
        float curr_sY = 0;
    } dspParams;

    float listenerX;
    float listenerY;

    enum Param
    {
        P_SMOOTHINGFACTOR,
        P_NUM
    };

    struct EffectData
    {
        float params[P_NUM];
    };

    int InternalRegisterEffectDefinition(UnityAudioEffectDefinition& definition)
    {
        int numparams = P_NUM;
        definition.paramdefs = new UnityAudioParameterDefinition[numparams];
        AudioPluginUtil::RegisterParameter(definition, "Smoothing", "", 1.f, 2.0f, 2.0f,
            1.0f, 1.0f, P_SMOOTHINGFACTOR, "Amount to smooth audio over time");
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
        if (dspParams.lowPass < PV_DSP_MIN_AUDIBLE_FREQ || dspParams.lowPass > PV_DSP_MAX_AUDIBLE_FREQ ||
            dspParams.dryGain <= 0.f || (dspParams.direcX == 0.f && dspParams.direcY == 0.f)) {
            memset(outbuffer, 0, length * outchannels * sizeof(float)); // play zero sound, as a debugging ref.
            //memcpy(outbuffer, inbuffer, length * outchannels * sizeof(float)); // dry sound
            return UNITY_AUDIODSP_ERR_UNSUPPORTED;
        }

        // length seems to be audio block size per channel for 1 channel


        // TODO: if use spatialization? - have a checkbox param? or consider using a spatialization plugin.


        // determine lerp factor ?
        float lerpFactor = 1.f / ((float)(length) * (float)data->params[P_SMOOTHINGFACTOR]);

        // TODO: Low pass filter? or leave it up to user effects?


        // do everything in-place in buffers???
        // basically coagulate channels into mono-data at the first "length" slots of inbuffer.
        float* inPtrMono = inbuffer; 
        const float* inputPtrIter = inbuffer;
        for (int i = 0; i < length; ++i)
        { // right now agnostic beyond stereo - w/ spatialization, might not be necesary?
            float val = 0;
            for (int j = 0; j < outchannels; ++j) {
                val += *inputPtrIter++;
            }
            *inPtrMono++ = (val) / outchannels;
        }
        inPtrMono = inbuffer;

        // incorporate wet gains, writing per-channel wet sum into outbuffer
        float targetRevGainA = FindGainA(dspParams.rt60, dspParams.wetGain);
        float targetRevGainB = FindGainB(dspParams.rt60, dspParams.wetGain);
        float targetRevGainC = FindGainC(dspParams.rt60, dspParams.wetGain);

        float currRevGainA = FindGainA(dspParams.curr_rt60, dspParams.curr_wetGain);
        float currRevGainB = FindGainB(dspParams.curr_rt60, dspParams.curr_wetGain);
        float currRevGainC = FindGainC(dspParams.curr_rt60, dspParams.curr_wetGain);
        float currGainSum = 0;
        float* outPtr = outbuffer;
        for (int i = 0; i < length; ++i) {
            float currVal = *inPtrMono * (currRevGainA + currRevGainB + currRevGainC) * 0.9;
            float targetVal = *(inPtrMono++) * (targetRevGainA + targetRevGainB + targetRevGainC) * 0.9; // this one goes to outbuffer

            *outPtr = 0;
            for (int j = 0; j < outchannels; ++j) {
                *(outPtr++) = std::lerp(currVal, targetVal, lerpFactor);
            }
        }

        // incorporate dry gains
        // TODO: determine directivity gains of the source for cardioid etc.
        float sDirectivityGainCurrent = 1.f; // omnidirectional
        float sDirectivityGainTarget = 1.f;

        float distX = listenerX - dspParams.sX;
        float distY = listenerY - dspParams.sY;
        float euclideanDistance = std::sqrt(distX * distX + distY * distY);
        euclideanDistance = (euclideanDistance < 1.f) ? 1.f : euclideanDistance;
        float targetDistanceAttenuation = 1.f / euclideanDistance;
        /// ///
        distX = listenerX - dspParams.curr_sX;
        distY = listenerY - dspParams.curr_sY;
        euclideanDistance = std::sqrt(distX * distX + distY * distY);
        euclideanDistance = (euclideanDistance < 1.f) ? 1.f : euclideanDistance;
        float currentDistanceAttenuation = targetDistanceAttenuation;

        float currDryGain = dspParams.curr_dryGain;
        float targetDryGain = dspParams.dryGain;
        //float targetDryGain = std::max(dspParams.dryGain, PV_DSP_MIN_DRY_GAIN);

        inPtrMono = inbuffer;
        outPtr = outbuffer;
        for (int i = 0; i < length; ++i)
        {
            float val = *inPtrMono++ * currDryGain * sDirectivityGainCurrent * currentDistanceAttenuation;

            // TODO: if spatializatoin, this should reflect that
            // out += in, for every in
            for (int j = 0; j < outchannels; ++j) {
                *(outPtr++) += val;
            }

            currDryGain = std::lerp(currDryGain, targetDryGain, lerpFactor);
            sDirectivityGainCurrent = std::lerp(sDirectivityGainCurrent, sDirectivityGainTarget, lerpFactor);
            currentDistanceAttenuation = std::lerp(currentDistanceAttenuation, targetDistanceAttenuation, lerpFactor);
        }

        // TODO: this can be simpler.        
        for (int i = 0; i < length; ++i) {
            dspParams.curr_direcX = std::lerp(dspParams.curr_direcX, dspParams.direcX, lerpFactor);
            dspParams.curr_direcY = std::lerp(dspParams.curr_direcY, dspParams.direcY, lerpFactor);
            dspParams.curr_wetGain = std::lerp(dspParams.curr_wetGain, dspParams.wetGain, lerpFactor);
            dspParams.curr_rt60 = std::lerp(dspParams.curr_direcX, dspParams.direcX, lerpFactor);
            //dspParams.forwardx, y = std::lerp(dspParams., dspParams., lerpFactor);
            dspParams.curr_sDirectX = std::lerp(dspParams.curr_sDirectX, dspParams.sDirectX, lerpFactor);
            dspParams.curr_sDirectY = std::lerp(dspParams.curr_sDirectY, dspParams.sDirectY, lerpFactor);
            dspParams.curr_sX = std::lerp(dspParams.curr_sX, dspParams.sX, lerpFactor);
            dspParams.curr_sY = std::lerp(dspParams.curr_sY, dspParams.sY, lerpFactor);
        }

        return UNITY_AUDIODSP_OK;
    }

    extern "C" UNITY_AUDIODSP_EXPORT_API bool uploadSignalAnalysis(
        float dryGain,
        float wetGain,
        float rt60,
        float lowPass,
        float direcX,
        float direcY,
        float sDirectX,
        float sDirectY,
        float sX,
        float sY) {
        // TODO: check validity of values here. if needed.

        dspParams.dryGain = dryGain;
        dspParams.wetGain = wetGain;
        dspParams.rt60 = rt60;
        dspParams.lowPass = lowPass;
        dspParams.direcX = direcX;
        dspParams.direcY = direcY;
        dspParams.sDirectX = sDirectX;
        dspParams.sDirectY = sDirectY;
        dspParams.sX = sX;
        dspParams.sY = sY;

        return true;
    }

    extern "C" UNITY_AUDIODSP_EXPORT_API bool updateListenerPos(float x, float y) {
        listenerX = x;
        listenerY = y;
        return true;
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
