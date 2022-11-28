#include "AudioPluginUtil.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace GPUVerbDSP
{
    float listenerX;
    float listenerY;

    enum class DirectivityPattern
    {
        Omni,
        Cardioid,
        // add more here
        SourceDirectivityPatternCount
    };

    typedef struct SourceData
    {
        // inherent to source
        DirectivityPattern sourcePattern;

        float sourceX;
        float sourceY;
        float sourceForwardX;
        float sourceForwardY;

        float curr_sourceX = 0;
        float curr_sourceY = 0;
        float curr_sourceForwardX = 0;
        float curr_sourceForwardY = 0;

        // Analyzer output
        float dryGain;
        float wetGain;
        float rt60;
        float lowPass;
        float direcX;
        float direcY;
        float sDirectivityX;
        float sDirectivityY;

        float curr_dryGain = 1.f;
        float curr_wetGain = 1.f;
        float curr_rt60 = 0.f;
        float curr_lowPass;
        float curr_direcX = 0;
        float curr_direcY = 0;
        float curr_sDirectivityX = 0;
        float curr_sDirectivityY = 0;
    };

    std::unordered_map<int, SourceData> sourceMap;

    enum Param
    {
        P_MIXERNUM,
        P_SMOOTHINGFACTOR,
        P_WETGAINRATIO,
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
        AudioPluginUtil::RegisterParameter(definition, "Source ID", "", 1.0f, 100.0f, 1.0f,
            1.0f, 1.0f, P_MIXERNUM, "The ID of an uploader script that outputs to this mixer should match Source ID");
        AudioPluginUtil::RegisterParameter(definition, "Smoothing", "", 1.f, 2.0f, 2.0f,
            1.0f, 1.0f, P_SMOOTHINGFACTOR, "Amount to smooth audio over time");
        AudioPluginUtil::RegisterParameter(definition, "WetGain Ratio", "", 0.0f, 1.0f, 0.1f,
            1.0f, 1.0f, P_WETGAINRATIO, "Ratio for how much the reverberant sound affects the audio.");
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
        EffectData* data = state->GetEffectData<EffectData>();

        // length seems to be audio block size per channel for 1 channel

        // TODO: if use spatialization? - have a checkbox param? or consider using a spatialization plugin.

        float lerpFactor = 1.f / ((float)(length) * (float)data->params[P_SMOOTHINGFACTOR]);

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

        // set output to 0 (necessary?)
        memset(outbuffer, 0, length * outchannels * sizeof(float));

        auto it = sourceMap.find((int)data->params[P_MIXERNUM]);
        if (it == sourceMap.end()) { return UNITY_AUDIODSP_ERR_UNSUPPORTED; }
        auto& source = it->second;
        // exit if input is invalid - shouldn't happen.
        if (source.lowPass < PV_DSP_MIN_AUDIBLE_FREQ || source.lowPass > PV_DSP_MAX_AUDIBLE_FREQ ||
            source.dryGain <= 0.f || (source.direcX == 0.f && source.direcY == 0.f)) {
            // play zero sound, as a debugging ref.
            return UNITY_AUDIODSP_ERR_UNSUPPORTED;
        }

        // TODO: Low pass filter? or leave it up to user effects?

        // incorporate wet gains, writing per-channel wet sum into outbuffer
        float targetRevGainA = FindGainA(source.rt60, source.wetGain);
        float targetRevGainB = FindGainB(source.rt60, source.wetGain);
        float targetRevGainC = FindGainC(source.rt60, source.wetGain);
        float currRevGainA = FindGainA(source.curr_rt60, source.curr_wetGain);
        float currRevGainB = FindGainB(source.curr_rt60, source.curr_wetGain);
        float currRevGainC = FindGainC(source.curr_rt60, source.curr_wetGain);
        float currGainSum = 0;
        float* outPtr = outbuffer;
        inPtrMono = inbuffer;
        for (int i = 0; i < length; ++i) {
            float valA = *inPtrMono * currRevGainA * data->params[P_WETGAINRATIO];
            float valB = *inPtrMono * currRevGainB * data->params[P_WETGAINRATIO];
            float valC = *inPtrMono++ * currRevGainC * data->params[P_WETGAINRATIO];
            for (int j = 0; j < outchannels; ++j) {
                *(outPtr++) += valA + valB + valC;
            }
            currRevGainA = std::lerp(currRevGainA, targetRevGainA, lerpFactor);
            currRevGainB = std::lerp(currRevGainB, targetRevGainB, lerpFactor);
            currRevGainC = std::lerp(currRevGainC, targetRevGainC, lerpFactor);
        }

        // incorporate dry gains:
            // if (sourcePattern == DirectivityPattern::Omni)
        float sDirectivityGainCurrent = 1.f;
        float sDirectivityGainTarget = 1.f;
        if (source.sourcePattern == DirectivityPattern::Cardioid) {
            sDirectivityGainCurrent = CardioidPattern(source.curr_sDirectivityX, source.curr_sDirectivityY,
                source.curr_sourceForwardX, source.curr_sourceForwardY);
            sDirectivityGainTarget = CardioidPattern(source.sDirectivityX, source.sDirectivityY,
                source.sourceForwardX, source.sourceForwardY);
        }

        float distX = listenerX - source.sourceX;
        float distY = listenerY - source.sourceY;
        float euclideanDistance = std::sqrt(distX * distX + distY * distY);
        euclideanDistance = (euclideanDistance < 1.f) ? 1.f : euclideanDistance;
        float targetDistanceAttenuation = 1.f / euclideanDistance;
        /// ///
        distX = listenerX - source.curr_sourceX;
        distY = listenerY - source.curr_sourceY;
        euclideanDistance = std::sqrt(distX * distX + distY * distY);
        euclideanDistance = (euclideanDistance < 1.f) ? 1.f : euclideanDistance;
        float currentDistanceAttenuation = 1.f / euclideanDistance;

        float currDryGain = source.curr_dryGain;
        float targetDryGain = (std::max)(source.dryGain, PV_DSP_MIN_DRY_GAIN);

        inPtrMono = inbuffer;
        outPtr = outbuffer;
        for (int i = 0; i < length; ++i)
        {
            float val = *inPtrMono++ * currDryGain * sDirectivityGainCurrent * currentDistanceAttenuation;

            // TODO: if spatialization, this should reflect that
            for (int j = 0; j < outchannels; ++j) {
                *(outPtr++) += val;
            }
            currDryGain = std::lerp(currDryGain, targetDryGain, lerpFactor);
            sDirectivityGainCurrent = std::lerp(sDirectivityGainCurrent, sDirectivityGainTarget, lerpFactor);
            currentDistanceAttenuation = std::lerp(currentDistanceAttenuation, targetDistanceAttenuation, lerpFactor);
        }

        // TODO: this can be simpler.
        source.curr_dryGain = currDryGain;
        for (int i = 0; i < length; ++i) {
            source.curr_direcX = std::lerp(source.curr_direcX, source.direcX, lerpFactor);
            source.curr_direcY = std::lerp(source.curr_direcY, source.direcY, lerpFactor);
            source.curr_wetGain = std::lerp(source.curr_wetGain, source.wetGain, lerpFactor);
            source.curr_rt60 = std::lerp(source.curr_rt60, source.rt60, lerpFactor);
            source.curr_sourceForwardX = std::lerp(source.curr_sourceForwardX, source.sourceForwardX, lerpFactor);
            source.curr_sourceForwardY = std::lerp(source.curr_sourceForwardY, source.sourceForwardY, lerpFactor);
            source.curr_sDirectivityX = std::lerp(source.curr_sDirectivityX, source.sDirectivityX, lerpFactor);
            source.curr_sDirectivityY = std::lerp(source.curr_sDirectivityY, source.sDirectivityY, lerpFactor);
            source.curr_sourceX = std::lerp(source.curr_sourceX, source.sourceX, lerpFactor);
            source.curr_sourceY = std::lerp(source.curr_sourceY, source.sourceY, lerpFactor);
        }
        
        return UNITY_AUDIODSP_OK;
    }

    //TODO: set up an "initialize" function to be called in C# to insert an ID into map to cut down on find, if, else code blocks
    extern "C" UNITY_AUDIODSP_EXPORT_API bool uploadSignalAnalysis(
        int id,
        float dryGain,
        float wetGain,
        float rt60,
        float lowPass,
        float direcX,
        float direcY,
        float sDirectX,
        float sDirectY) {
        auto it = sourceMap.find(id);
        if (it == sourceMap.end()) {
            SourceData data{
               .dryGain = dryGain,
               .wetGain = wetGain,
               .rt60 = rt60,
               .lowPass = lowPass,
               .direcX = direcX,
               .direcY = direcY,
               .sDirectivityX = sDirectX,
               .sDirectivityY = sDirectY
            };
            sourceMap.insert({ id, data });
        }
        else {
            it->second.dryGain = dryGain;
            it->second.wetGain = wetGain;
            it->second.rt60 = rt60;
            it->second.lowPass = lowPass;
            it->second.direcX = direcX;
            it->second.direcY = direcY;
            it->second.sDirectivityX = sDirectX;
            it->second.sDirectivityY = sDirectY;
        }
        return true;
    }

    extern "C" UNITY_AUDIODSP_EXPORT_API bool updateListenerPos(float x, float y) {
        listenerX = x;
        listenerY = y;
        return true;
    }

    extern "C" UNITY_AUDIODSP_EXPORT_API bool updateSourcePos(
        int id,
        float x, float y,
        float forwardX, float forwardY) {
        auto it = sourceMap.find(id);
        if (it == sourceMap.end()) {
            SourceData data{
               .sourceX = x,
               .sourceY = y,
               .sourceForwardX = forwardX,
               .sourceForwardY = forwardY,
            };
            sourceMap.insert({ id, data });
        }
        else {
            it->second.sourceX = x;
            it->second.sourceY = y;
            it->second.sourceForwardX = forwardX;
            it->second.sourceForwardY = forwardY;
        }
        return true;
    }

    extern "C" UNITY_AUDIODSP_EXPORT_API bool setSourcePattern(
        int id,
        int pattern) {
        auto it = sourceMap.find(id);
        if (it == sourceMap.end()) {
            SourceData data{
               .sourcePattern = static_cast<DirectivityPattern>(pattern),
            };
            sourceMap.insert({ id, data });
        }
        else {
            it->second.sourcePattern = static_cast<DirectivityPattern>(pattern);
        }
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
