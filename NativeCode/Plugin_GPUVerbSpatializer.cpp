// Please note that this will only work on Unity 5.2 or higher.
#include "AudioPluginUtil.h"
#include <array>
#include <cmath>

extern float reverbABuffer[];
extern float reverbBBuffer[];
extern float reverbCBuffer[];

namespace GPUVerbSpatializer {
    inline bool IsHostCompatible(UnityAudioEffectState* state)
    {
        // Somewhat convoluted error checking here because hostapiversion is only supported from SDK version 1.03 (i.e. Unity 5.2) and onwards.
        // Since we are only checking for version 0x010300 here, we can't use newer fields in the UnityAudioSpatializerData struct, such as minDistance and maxDistance.
        return
            state->structsize >= sizeof(UnityAudioEffectState) &&
            state->hostapiversion >= 0x010300;
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
    const constexpr float TSTAR = 0.1f; // 100ms

    enum class DirectivityPattern {
        Omni,
        Cardioid,
        // add more here
        SourceDirectivityPatternCount
    };

    enum Param {
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

    struct EffectData {
        std::array<float, numParams> p;

        float curr_sourceX = 0;
        float curr_sourceY = 0;
        float curr_sourceZ = 0;

        float curr_sourceForwardX = 0; 
        float curr_sourceForwardZ = 0;

        float curr_dryGain = 1.f;
        float curr_wetGain = 1.f;
        float curr_rt60 = 0.f;
        //float curr_lowPass; 
        float curr_direcX = 0;
        float curr_direcZ = 0; 
        float curr_sDirectivityX = 0;
        float curr_sDirectivityZ = 0; // this actually corresponds to unity z. confusing
    };

    int InternalRegisterEffectDefinition(UnityAudioEffectDefinition& definition)
    {
        int numparams = numParams;
        definition.paramdefs = new UnityAudioParameterDefinition[numparams];
        // hacky way of reading parameters for spatializer effectdata.
        AudioPluginUtil::RegisterParameter(definition, "Spatialize", "",
            0.f, 1.f,
            1.f, 1.0f, 1.0f, Param::SPATIALIZE, "Spatialize bool - special parameter");
        AudioPluginUtil::RegisterParameter(definition, "Mute Dry", "",
            0.f, 1.f,
            0.f, 1.0f, 1.0f, Param::MUTE_DRY, "Mute dry bool - special parameter");
        AudioPluginUtil::RegisterParameter(definition, "Smoothing", "",
            0.f, 10.f,
            2.f, 1.0f, 1.0f, Param::SMOOTHING_FACTOR, "Audio smoothing amount");
        AudioPluginUtil::RegisterParameter(definition, "W/G R", "",
            0.f, 10.f,
            0.1f, 1.0f, 1.0f, Param::WET_GAIN_RATIO, "Wet gain ratio");
        AudioPluginUtil::RegisterParameter(definition, "S Pattern", "",
            0, (float)DirectivityPattern::SourceDirectivityPatternCount,
            (float)DirectivityPattern::Omni, 1.0f, 1.0f, Param::sourcePattern, "Special parameter, only modify in-code");
        AudioPluginUtil::RegisterParameter(definition, "Dry Gain", "",
            0, 100.f,
            0, 1.0f, 1.0f, Param::dryGain, "Special parameter, only modify in-code");
        AudioPluginUtil::RegisterParameter(definition, "Wet Gain", "",
            -100.f, 100.f,
            0, 1.0f, 1.0f, Param::wetGain, "Special parameter, only modify in-code");
        AudioPluginUtil::RegisterParameter(definition, "RT60", "",
            -100.f, 100.f,
            0, 1.0f, 1.0f, Param::rt60, "Special parameter, only modify in-code");
        AudioPluginUtil::RegisterParameter(definition, "Low Pass", "",
            PV_DSP_MIN_AUDIBLE_FREQ, PV_DSP_MAX_AUDIBLE_FREQ,
            0, 1.0f, 1.0f, Param::lowPass, "Special parameter, only modify in-code");
        AudioPluginUtil::RegisterParameter(definition, "Direc X", "",
            -100.f, 100.f,
            0, 1.0f, 1.0f, Param::direcX, "Special parameter, only modify in-code");
        AudioPluginUtil::RegisterParameter(definition, "Direc Y", "",
            -100.f, 100.f,
            0, 1.0f, 1.0f, Param::direcY, "Special parameter, only modify in-code");
        AudioPluginUtil::RegisterParameter(definition, "SDirec X", "",
            -100.f, 100.f,
            0, 1.0f, 1.0f, Param::sDirectivityX, "Special parameter, only modify in-code");
        AudioPluginUtil::RegisterParameter(definition, "SDirec Y", "",
            -100.f, 100.f,
            0, 1.0f, 1.0f, Param::sDirectivityY, "Special parameter, only modify in-code");
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

    inline float FindGainA(float rt60, float wetGain)
    {
        if (rt60 > PV_DSP_T_ER_2)
        {
            return 0.f;
        }
        //else if (rt60 < PV_DSP_T_ER_1)
        //{
        //    return 1.f;
        //}

        float term1 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_2);
        float term2 = std::pow(10.f, -3.f * TSTAR / rt60);
        float term3 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_1);
        float a = wetGain * (term1 - term2) / (term1 - term3);
        return (std::min)(1.f, a);
    }

    inline float FindGainB(float rt60, float wetGain)
    {
        if (rt60 < PV_DSP_T_ER_1)
        {
            return 0.f;
        }

        float term2 = std::pow(10.f, -3.f * TSTAR / rt60);

        // case we want j + 1 instead of j
        if (rt60 > PV_DSP_T_ER_2)
        {
            float term1 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_3);
            float term3 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_2);
            float a = wetGain * (term1 - term2) / (term1 - term3);
            return a;
        }
        else
        {
            float term1 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_2);
            float term3 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_1);
            float a = wetGain * (term1 - term2) / (term1 - term3);
            return wetGain - a;
        }
    }

    inline float FindGainC(float rt60, float wetGain) {
        if (rt60 > PV_DSP_T_ER_3) {
            return 1.f;
        }
        else if (rt60 < PV_DSP_T_ER_2) {
            return 0.f;
        }

        float term1 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_3);
        float term2 = std::pow(10.f, -3.f * TSTAR / rt60);
        float term3 = std::pow(10.f, -3.f * TSTAR / PV_DSP_T_ER_2);
        float a = wetGain * (term1 - term2) / (term1 - term3);
        return wetGain - a;
    }

    inline float CardioidPattern(const float direcX, const float direcY,
        const float forwardX, const float forwardY) {
        float dotValue = direcX * forwardX + direcY * forwardY; // dot product
        float cardioid = (1.f + dotValue) / 2.f;
        return (cardioid > PV_DSP_MIN_DRY_GAIN) ? cardioid : PV_DSP_MIN_DRY_GAIN;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK ProcessCallback(UnityAudioEffectState* state, float* inbuffer, float* outbuffer, unsigned int length, int inchannels, int outchannels) {
        // Check that the host API supports this feature
        if (!IsHostCompatible(state) || state->spatializerdata == NULL) {
            memcpy(outbuffer, inbuffer, length * outchannels * sizeof(float)); // play raw audio
            return UNITY_AUDIODSP_OK;
        }

        EffectData* data = state->GetEffectData<EffectData>();
        // Exit if input is invalid - shouldn't happen with the hacky parameter passing
        if (data->p[Param::lowPass] < PV_DSP_MIN_AUDIBLE_FREQ ||
            data->p[Param::lowPass] > PV_DSP_MAX_AUDIBLE_FREQ ||
            data->p[Param::dryGain] <= 0.f ||
            (data->p[Param::direcX] == 0.f && data->p[Param::direcY] == 0.f)) {
            memset(outbuffer, 0, length * outchannels * sizeof(float)); // output no sound, as debugging
            return UNITY_AUDIODSP_ERR_UNSUPPORTED;
        }
        // Downmix channels into mono-data
        float* inPtrMono = inbuffer;
        const float* inputPtrIter = inbuffer;
        for (int i = 0; i < length; ++i) {
            float val = 0;
            for (int j = 0; j < outchannels; ++j) {
                val += *inputPtrIter++;
            }
            *inPtrMono++ = (val) / outchannels;
        }


        float lerpFactor = 1.f / ((float)(length)*data->p[Param::SMOOTHING_FACTOR]);

        // Possible to add low pass filter here - defer to using Unity's mixer effects?


        // incorporate wet gains
        float targetRevGainA = FindGainA(data->p[Param::rt60], data->p[Param::wetGain]);
        float targetRevGainB = FindGainB(data->p[Param::rt60], data->p[Param::wetGain]);
        float targetRevGainC = FindGainC(data->p[Param::rt60], data->p[Param::wetGain]);
        float currRevGainA = FindGainA(data->curr_rt60, data->curr_wetGain);
        float currRevGainB = FindGainB(data->curr_rt60, data->curr_wetGain);
        float currRevGainC = FindGainC(data->curr_rt60, data->curr_wetGain);
        float* outPtr = outbuffer;
        inPtrMono = inbuffer;

        float* reverbA = reverbABuffer;
        float* reverbB = reverbBBuffer;
        float* reverbC = reverbCBuffer;
        for (int i = 0; i < length; ++i) {
            float inVal = *inPtrMono++;
            for (int j = 0; j < outchannels; ++j) {
                // send vals to separate buffers to apply proper reverb effect on overall sound mix
                reverbA[i * outchannels + j] += inVal * currRevGainA * data->p[Param::WET_GAIN_RATIO];
                reverbB[i * outchannels + j] += inVal * currRevGainB * data->p[Param::WET_GAIN_RATIO];
                reverbC[i * outchannels + j] += inVal * currRevGainC * data->p[Param::WET_GAIN_RATIO];
            }
            currRevGainA = std::lerp(currRevGainA, targetRevGainA, lerpFactor);
            currRevGainB = std::lerp(currRevGainB, targetRevGainB, lerpFactor);
            currRevGainC = std::lerp(currRevGainC, targetRevGainC, lerpFactor);
        }

        // DRY GAIN CALCULATION
        float* L = state->spatializerdata->listenermatrix;
        float* S = state->spatializerdata->sourcematrix;
        float sourceX = S[12];
        float sourceY = S[13];
        float sourceZ = S[14];

        float listenerScaleSquared = 1.0f / (L[1] * L[1] + L[5] * L[5] + L[9] * L[9]);
        // transpose/inverse of rotation * translation
        float listenerX = -listenerScaleSquared * (L[0] * L[12] + L[1] * L[13] + L[2] * L[14]);
        float listenerY = -listenerScaleSquared * (L[4] * L[12] + L[5] * L[13] + L[6] * L[14]);
        float listenerZ = -listenerScaleSquared * (L[8] * L[12] + L[9] * L[13] + L[10] * L[14]); // the z position in-unity

        float dist1 = listenerX - sourceX;
        float dist2 = listenerY - sourceY;
        float dist3 = listenerZ - sourceZ;
        float euclideanDistance = std::sqrt(dist1 * dist1 + dist2 * dist2 + dist3 * dist3);
        euclideanDistance = (euclideanDistance < 1.f) ? 1.f : euclideanDistance;
        float targetDistanceAttenuation = 1.f / euclideanDistance;

        dist1 = listenerX - data->curr_sourceX;
        dist2 = listenerY - data->curr_sourceY;
        dist3 = listenerZ - data->curr_sourceZ;
        euclideanDistance = std::sqrt(dist1 * dist1 + dist2 * dist2 + dist3 * dist3);
        euclideanDistance = (euclideanDistance < 1.f) ? 1.f : euclideanDistance;
        float currDistanceAttenuation = 1.f / euclideanDistance;

        // x and z components of z-facing vector transform
        float mag = std::sqrt(S[8] * S[8] + S[9] * S[9] + S[10] * S[10]);
        float sourceForwardX = S[8] / mag;
        float sourceForwardZ = S[10] / mag;

        // if (sourcePattern == DirectivityPattern::Omni)
        float currSDirectivityGain = 1.f;
        float targetSDirectivityGain = 1.f;
        if ((int)data->p[Param::sourcePattern] == static_cast<int>(DirectivityPattern::Cardioid)) {
            currSDirectivityGain = CardioidPattern(data->curr_sDirectivityX, data->curr_sDirectivityZ,
                data->curr_sourceForwardX, data->curr_sourceForwardZ);
            targetSDirectivityGain = CardioidPattern(data->p[Param::sDirectivityX], data->p[Param::sDirectivityY],
                sourceForwardX, sourceForwardZ);
        }

        float targetDryGain = (std::max)(data->p[Param::dryGain], PV_DSP_MIN_DRY_GAIN);

        // Spatialization: determine panning current and target values
        bool spatialize = data->p[Param::SPATIALIZE] && (inchannels == 2 && outchannels == 2);

        float targetLeft = 1.f, targetRight = 1.f;
        float currLeft = 1.f, currRight = 1.f;
        if (spatialize) {
            mag = std::sqrt(L[2] * L[2] + L[6] * L[6] + L[10] * L[10]);
            float listenerForwardX = L[2] / mag;
            float listenerForwardY = L[10] / mag;

            float angle = std::atan2f(listenerForwardY, listenerForwardX);
            float phi = std::atan2f(data->p[Param::direcY], data->p[Param::direcX]); // arrival direction of sound
            float theta = (angle - phi) / 2.f;
            float ct = std::cos(theta);
            float st = std::sin(theta);
            targetLeft = PV_DSP_INV_SQRT_2 * std::abs(ct - st);
            targetRight = PV_DSP_INV_SQRT_2 * std::abs(ct + st); // avoid discontinutiy (popping)

            phi = std::atan2f(data->curr_direcZ, data->curr_direcX);
            theta = (angle - phi) / 2.f;
            ct = std::cos(theta);
            st = std::sin(theta);
            currLeft = PV_DSP_INV_SQRT_2 * std::abs(ct - st);
            currRight = PV_DSP_INV_SQRT_2 * std::abs(ct + st);
        }

        inPtrMono = inbuffer;
        outPtr = outbuffer;
        for (int i = 0; i < length; ++i) {
            if (data->p[Param::MUTE_DRY]) { // inside loop to loop-update reverb parameters
                memset(outbuffer, 0, length * outchannels * sizeof(float));
            } else {
                float val = *inPtrMono++ * data->curr_dryGain * currSDirectivityGain * currDistanceAttenuation;
                if (!spatialize) {
                    for (int j = 0; j < outchannels; ++j) { // upmix across channels if not spatializing
                        *(outPtr++) = val;
                    }
                } else {
                    *(outPtr++) = val * currLeft; //+=
                    *(outPtr++) = val * currRight;
                    currLeft = std::lerp(currLeft, targetLeft, lerpFactor);
                    currRight = std::lerp(currRight, targetRight, lerpFactor);
                }
            }

            data->curr_dryGain = std::lerp(data->curr_dryGain, targetDryGain, lerpFactor);
            currSDirectivityGain = std::lerp(currSDirectivityGain, targetSDirectivityGain, lerpFactor);
            currDistanceAttenuation = std::lerp(currDistanceAttenuation, targetDistanceAttenuation, lerpFactor);

            // lerp here instead of another loop. 
            // could be moved to more appropriate places (eg lerp wet during wet mixing), but messier code
            data->curr_direcX = std::lerp(data->curr_direcX, data->p[Param::direcX], lerpFactor);
            data->curr_direcZ = std::lerp(data->curr_direcZ, data->p[Param::direcY], lerpFactor);

            data->curr_wetGain = std::lerp(data->curr_wetGain, data->p[Param::wetGain], lerpFactor);
            data->curr_rt60 = std::lerp(data->curr_rt60, data->p[Param::rt60], lerpFactor);

            data->curr_sourceForwardX = std::lerp(data->curr_sourceForwardX, sourceForwardX, lerpFactor);
            data->curr_sourceForwardZ = std::lerp(data->curr_sourceForwardZ, sourceForwardZ, lerpFactor);
            data->curr_sDirectivityX = std::lerp(data->curr_sDirectivityX, data->p[Param::sDirectivityX], lerpFactor);
            data->curr_sDirectivityZ = std::lerp(data->curr_sDirectivityZ, data->p[Param::sDirectivityY], lerpFactor);
            data->curr_sourceX = std::lerp(data->curr_sourceX, sourceX, lerpFactor);
            data->curr_sourceY = std::lerp(data->curr_sourceY, sourceY, lerpFactor);
            data->curr_sourceZ = std::lerp(data->curr_sourceZ, sourceZ, lerpFactor);
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
