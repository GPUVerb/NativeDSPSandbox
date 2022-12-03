// Please note that this will only work on Unity 5.2 or higher.
#include "AudioPluginUtil.h"
#include <array>
#include <cmath>

namespace GPUVerbSpatializer
{
    inline bool IsHostCompatible(UnityAudioEffectState* state)
    {
        // Somewhat convoluted error checking here because hostapiversion is only supported from SDK version 1.03 (i.e. Unity 5.2) and onwards.
        // Since we are only checking for version 0x010300 here, we can't use newer fields in the UnityAudioSpatializerData struct, such as minDistance and maxDistance.
        return
            state->structsize >= sizeof(UnityAudioEffectState) &&
            state->hostapiversion >= 0x010300;
    }

    enum class DirectivityPattern {
        Omni,
        Cardioid,
        // add more here
        SourceDirectivityPatternCount
    };

    enum Param {
        sourcePattern, // corresponds to the enum DirectivityPattern
        //sourceX, // unity spatializer parameters are off by some?
        //sourceY,
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

    struct EffectData {
        std::array<float, numParams> p;

        float curr_sourceX = 0;
        float curr_sourceY = 0;
        float curr_sourceForwardX = 0;
        float curr_sourceForwardY = 0;

        float curr_dryGain = 1.f;
        float curr_wetGain = 1.f;
        float curr_rt60 = 0.f;
        float curr_lowPass;
        float curr_direcX = 0;
        float curr_direcY = 0;
        float curr_sDirectivityX = 0;
        float curr_sDirectivityY = 0;
    };

    int InternalRegisterEffectDefinition(UnityAudioEffectDefinition& definition)
    {
        int numparams = numParams;
        definition.paramdefs = new UnityAudioParameterDefinition[numparams];
        AudioPluginUtil::RegisterParameter(definition, "S Pattern", "",
            0, (float)DirectivityPattern::SourceDirectivityPatternCount,
            (float)DirectivityPattern::Omni, 1.0f, 1.0f, Param::sourcePattern, "");
        AudioPluginUtil::RegisterParameter(definition, "SForw X", "",
            -100.f, 100.f,
            0.f, 1.0f, 1.0f, Param::sourceForwardX, "");
        AudioPluginUtil::RegisterParameter(definition, "SForw Y", "",
            -100.f, 100.f,
            0, 1.0f, 1.0f, Param::sourceForwardY);
        AudioPluginUtil::RegisterParameter(definition, "Dry Gain", "",
            0, 100.f,
            0, 1.0f, 1.0f, Param::dryGain);
        AudioPluginUtil::RegisterParameter(definition, "Wet Gain", "",
            -100.f, 100.f,
            0, 1.0f, 1.0f, Param::wetGain);
        AudioPluginUtil::RegisterParameter(definition, "RT60", "",
            -100.f, 100.f,
            0, 1.0f, 1.0f, Param::rt60);
        AudioPluginUtil::RegisterParameter(definition, "Low Pass", "",
            0.f, 100.f,
            0, 1.0f, 1.0f, Param::lowPass);
        AudioPluginUtil::RegisterParameter(definition, "Direc X", "",
            -100.f, 100.f,
            0, 1.0f, 1.0f, Param::direcX);
        AudioPluginUtil::RegisterParameter(definition, "Direc Y", "",
            -100.f, 100.f,
            0, 1.0f, 1.0f, Param::direcY);
        AudioPluginUtil::RegisterParameter(definition, "SDirec X", "",
            -100.f, 100.f,
            0, 1.0f, 1.0f, Param::sDirectivityX);
        AudioPluginUtil::RegisterParameter(definition, "SDirec Y", "",
            -100.f, 100.f,
            0, 1.0f, 1.0f, Param::sDirectivityY);
        definition.flags |= UnityAudioEffectDefinitionFlags_IsSpatializer;
        return numparams;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK CreateCallback(UnityAudioEffectState* state)
    {
        EffectData* effectdata = new EffectData;
        memset(effectdata, 0, sizeof(EffectData));
        state->effectdata = effectdata;
        AudioPluginUtil::InitParametersFromDefinitions(InternalRegisterEffectDefinition, effectdata->p.data());
        return UNITY_AUDIODSP_OK;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK ReleaseCallback(UnityAudioEffectState* state) {
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

    inline float FindGainC(float rt60, float wetGain) {
        if (rt60 > PV_DSP_T_ER_3) {
            return 1.f;
        }
        else if (rt60 < PV_DSP_T_ER_2) {
            return 0.f;
        }

        float gain = wetGain;
        float term1 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_3);
        float term2 = std::pow(10.f, -3.f * TSTAR / rt60);
        float term3 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_2);
        float a = gain * (term1 - term2) / (term1 - term3);
        return gain - a;
    }

    inline float CardioidPattern(const float direcX, const float direcY,
        const float forwardX, const float forwardY) {
        float dotValue = direcX * forwardX + direcY * forwardY; // dot product
        float cardioid = (1.f + dotValue) / 2.f;
        return (cardioid > PV_DSP_MIN_DRY_GAIN) ? cardioid : PV_DSP_MIN_DRY_GAIN;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK ProcessCallback(UnityAudioEffectState* state, float* inbuffer, float* outbuffer, unsigned int length, int inchannels, int outchannels)
    {
        // Check that I/O formats are right and that the host API supports this feature
        if (inchannels != 2 || outchannels != 2 ||
            !IsHostCompatible(state) || state->spatializerdata == NULL) {
            memset(outbuffer, 0, length * outchannels * sizeof(float)); // output no sound
            //memcpy(outbuffer, inbuffer, length * outchannels * sizeof(float));
            return UNITY_AUDIODSP_OK;
        }

        EffectData* data = state->GetEffectData<EffectData>();
        // exit if input is invalid - shouldn't happen.
        if (data->p[Param::lowPass] < PV_DSP_MIN_AUDIBLE_FREQ ||
            data->p[Param::lowPass] > PV_DSP_MAX_AUDIBLE_FREQ ||
            data->p[Param::dryGain] <= 0.f ||
            (data->p[Param::direcX] == 0.f && data->p[Param::direcY] == 0.f)) {
            memcpy(outbuffer, inbuffer, length * outchannels * sizeof(float));
            return UNITY_AUDIODSP_ERR_UNSUPPORTED;
        }

        float lerpFactor = 1.f / ((float)(length) * 2.f); // TODO: expose as smoothing parameter
        // basically coagulate channels into mono-data at the first "length" slots of inbuffer.
        float* inPtrMono = inbuffer;
        const float* inputPtrIter = inbuffer;
        for (int i = 0; i < length; ++i) { // right now agnostic beyond stereo - w/ spatialization, might not be necesary?
            float val = 0;
            for (int j = 0; j < outchannels; ++j) {
                val += *inputPtrIter++;
            }
            *inPtrMono++ = (val) / outchannels;
        }

        // TODO: Low pass filter? or leave it up to user effects?

        // incorporate wet gains, writing per-channel wet sum into outbuffer
        float targetRevGainA = FindGainA(data->p[Param::rt60], data->p[Param::wetGain]);
        float targetRevGainB = FindGainB(data->p[Param::rt60], data->p[Param::wetGain]);
        float targetRevGainC = FindGainC(data->p[Param::rt60], data->p[Param::wetGain]);
        float currRevGainA = FindGainA(data->curr_rt60, data->curr_wetGain);
        float currRevGainB = FindGainB(data->curr_rt60, data->curr_wetGain);
        float currRevGainC = FindGainC(data->curr_rt60, data->curr_wetGain);
        float currGainSum = 0;
        float* outPtr = outbuffer;
        inPtrMono = inbuffer;
        for (int i = 0; i < length; ++i) {
            float valA = *inPtrMono * currRevGainA * 0.1f; // TOOD: expose as wetgainratio
            float valB = *inPtrMono * currRevGainB * 0.1f;
            float valC = *inPtrMono++ * currRevGainC * 0.1f;
            for (int j = 0; j < outchannels; ++j) {
                *(outPtr++) = valA + valB + valC;
            }
            currRevGainA = std::lerp(currRevGainA, targetRevGainA, lerpFactor);
            currRevGainB = std::lerp(currRevGainB, targetRevGainB, lerpFactor);
            currRevGainC = std::lerp(currRevGainC, targetRevGainC, lerpFactor);
        }

        // incorporate dry gains:
            // if (sourcePattern == DirectivityPattern::Omni)
        float currSDirectivityGain = 1.f;
        float targetSDirectivityGain = 1.f;
        //if (data->p[EffectData::sourcePattern] == DirectivityPattern::Cardioid) {
        //    sDirectivityGainCurrent = CardioidPattern(source.curr_sDirectivityX, source.curr_sDirectivityY,
        //        source.curr_sourceForwardX, source.curr_sourceForwardY);
        //    sDirectivityGainTarget = CardioidPattern(source.sDirectivityX, source.sDirectivityY,
        //        source.sourceForwardX, source.sourceForwardY);
        //}



        float* L = state->spatializerdata->listenermatrix;
        float* s = state->spatializerdata->sourcematrix;
        float sourceX = s[12];
            //float py = s[13];
        float sourceY = s[14];

        float listenerScaleSquared = 1.0f / (L[1] * L[1] + L[5] * L[5] + L[9] * L[9]);
        // transpose/inverse of rotation * translation
        float listenerX = -listenerScaleSquared * (L[0] * L[12] + L[1] * L[13] + L[2] * L[14]);
        float listenerY = -listenerScaleSquared * (L[8] * L[12] + L[9] * L[13] + L[10] * L[14]); // the z position in-unity

        float distX = listenerX - sourceX;
        float distY = listenerY - sourceY;
        float euclideanDistance = std::sqrt(distX * distX + distY * distY);
        euclideanDistance = (euclideanDistance < 1.f) ? 1.f : euclideanDistance;
        float targetDistanceAttenuation = 1.f / euclideanDistance;
        /// ///
        distX = listenerX - data->curr_sourceX;
        distY = listenerY - data->curr_sourceY;
        euclideanDistance = std::sqrt(distX * distX + distY * distY);
        euclideanDistance = (euclideanDistance < 1.f) ? 1.f : euclideanDistance;
        float currDistanceAttenuation = 1.f / euclideanDistance;

        float currDryGain = data->curr_dryGain;
        float targetDryGain = (std::max)(data->p[Param::dryGain], PV_DSP_MIN_DRY_GAIN);

        inPtrMono = inbuffer;
        outPtr = outbuffer;
        for (int i = 0; i < length; ++i)
        {
            float val = *inPtrMono++ * currDryGain * currSDirectivityGain * currDistanceAttenuation;

            // TODO: if spatialization, this should reflect that
            for (int j = 0; j < outchannels; ++j) {
                *(outPtr++) += val;
            }
            currDryGain = std::lerp(currDryGain, targetDryGain, lerpFactor);
            currSDirectivityGain = std::lerp(currSDirectivityGain, targetSDirectivityGain, lerpFactor);
            currDistanceAttenuation = std::lerp(currDistanceAttenuation, targetDistanceAttenuation, lerpFactor);
        }

        // TODO: this can be simpler.
        data->curr_dryGain = currDryGain;
        for (int i = 0; i < length; ++i) {
            data->curr_direcX         = std::lerp(data->curr_direcX,         data->p[Param::direcX], lerpFactor);
            data->curr_direcY         = std::lerp(data->curr_direcY,         data->p[Param::direcY], lerpFactor);
            data->curr_wetGain        = std::lerp(data->curr_wetGain,        data->p[Param::wetGain], lerpFactor);
            data->curr_rt60           = std::lerp(data->curr_rt60,           data->p[Param::rt60], lerpFactor);
            //data->curr_sourceForwardX = std::lerp(data->curr_sourceForwardX, data->p[Param::sourceForwardX], lerpFactor);
            //data->curr_sourceForwardY = std::lerp(data->curr_sourceForwardY, data->p[Param::sourceForwardY], lerpFactor);
            //data->curr_sDirectivityX  = std::lerp(data->curr_sDirectivityX,  data->p[Param::sDirectivityX], lerpFactor);
            //data->curr_sDirectivityY  = std::lerp(data->curr_sDirectivityY,  data->p[Param::sDirectivityY], lerpFactor);
            data->curr_sourceX        = std::lerp(data->curr_sourceX,        sourceX, lerpFactor);
            data->curr_sourceY        = std::lerp(data->curr_sourceY,        sourceY, lerpFactor);
        }

        return UNITY_AUDIODSP_OK;
    }


    // leave these alone

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK SetFloatParameterCallback(UnityAudioEffectState* state, int index, float value)
    {
        EffectData* data = state->GetEffectData<EffectData>();
        if (index >= Param::numParams)
            return UNITY_AUDIODSP_ERR_UNSUPPORTED;
        data->p[index] = value;
        return UNITY_AUDIODSP_OK;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK GetFloatParameterCallback(UnityAudioEffectState* state, int index, float* value, char* valuestr)
    {
        EffectData* data = state->GetEffectData<EffectData>();
        if (index >= Param::numParams)
            return UNITY_AUDIODSP_ERR_UNSUPPORTED;
        if (value != NULL)
            *value = data->p[index];
        if (valuestr != NULL)
            valuestr[0] = 0;
        return UNITY_AUDIODSP_OK;
    }

    int UNITY_AUDIODSP_CALLBACK GetFloatBufferCallback(UnityAudioEffectState* state, const char* name, float* buffer, int numsamples)
    {
        return UNITY_AUDIODSP_OK;
    }
}
