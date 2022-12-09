#include "AudioPluginUtil.h"

//float reverbmixbuffer[65536] = { 0 };
float reverbABuffer[4096] = { 0 };
float reverbBBuffer[4096] = { 0 };
float reverbCBuffer[4096] = { 0 };

extern "C" UNITY_AUDIODSP_EXPORT_API float getReverbBuf(float** buf, int bufidx) {
    if (bufidx == 0) {
        *buf = reverbABuffer;
    }
    else if (bufidx == 1) {
        *buf = reverbBBuffer;
    }
    else if (bufidx == 2) {
        *buf = reverbCBuffer;
    }
    else {
        return false;
    }
    return true;
}

extern "C" UNITY_AUDIODSP_EXPORT_API bool zeroReverb(int bufidx) { // this correctly sets & has c# response.
    if (bufidx == 0) {
        memset(reverbABuffer, 0.f, sizeof(reverbABuffer));
    }
    else if (bufidx == 1) {
        memset(reverbBBuffer, 0.f, sizeof(reverbBBuffer));
    }
    else if (bufidx == 2) {
        memset(reverbCBuffer, 0.f, sizeof(reverbCBuffer));
    }
    else {
        return false;
    }
    return true;
}

//below: possible to implement as an overall effect, as opposed to multiple mixers.

class Random
{
public:
    inline void Seed(unsigned long _seed)
    {
        seed = _seed;
    }

    inline unsigned int Get()
    {
        seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF;
        return seed ^ (seed >> 16);
    }

    inline float GetFloat(float minval, float maxval)
    {
        return minval + (maxval - minval) * (Get() & 0xFFFFFF) * (const float)(1.0f / (float)0xFFFFFF);
    }

protected:
    unsigned int seed;
};

namespace VerbEffect {
    const int MAXTAPS = 1024;

    enum
    {
        P_DELAYTIME,
        P_DIFFUSION,
        P_NUM
    };

    struct InstanceChannel
    {
        struct Tap
        {
            int pos;
            float amp;
        };
        struct Delay
        {
            enum { MASK = 0xFFFFF };
            int writepos;
            inline void Write(float x)
            {
                writepos = (writepos + MASK) & MASK;
                data[writepos] = x;
            }

            inline float Read(int delay) const
            {
                return data[(writepos + delay) & MASK];
            }

            float data[MASK + 1];
        };
        Tap taps[1024];
        Delay delay;
    };

    struct EffectData
    {
        float p[P_NUM];
        Random random;
        InstanceChannel ch[2];
    };

    int InternalRegisterEffectDefinition(UnityAudioEffectDefinition& definition)
    {
        int numparams = P_NUM;
        definition.paramdefs = new UnityAudioParameterDefinition[numparams];
        AudioPluginUtil::RegisterParameter(definition, "Delay Time", "", 0.0f, 5.0f, 2.0f, 1.0f, 1.0f, P_DELAYTIME, "Delay time in seconds");
        AudioPluginUtil::RegisterParameter(definition, "Diffusion", "%", 0.0f, 1.0f, 0.5f, 100.0f, 1.0f, P_DIFFUSION, "Diffusion amount");
        return numparams;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK CreateCallback(UnityAudioEffectState* state)
    {
        EffectData* effectdata = new EffectData;
        memset(effectdata, 0, sizeof(EffectData));
        state->effectdata = effectdata;
        AudioPluginUtil::InitParametersFromDefinitions(InternalRegisterEffectDefinition, effectdata->p);
        return UNITY_AUDIODSP_OK;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK ReleaseCallback(UnityAudioEffectState* state)
    {
        EffectData* data = state->GetEffectData<EffectData>();
        delete data;
        return UNITY_AUDIODSP_OK;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK ProcessCallback(UnityAudioEffectState* state, float* inbuffer, float* outbuffer, unsigned int length, int inchannels, int outchannels)
    {
        if (inchannels != 2 || outchannels != 2) {
            memcpy(outbuffer, inbuffer, length * outchannels * sizeof(float));
            return UNITY_AUDIODSP_OK;
        }
        memcpy(outbuffer, inbuffer, length * outchannels * sizeof(float));
        //EffectData* data = state->GetEffectData<EffectData>();
        //// 0.59683064
        //const float delaytime = data->p[P_DELAYTIME] * state->samplerate + 1.0f; // 0.5f * 44100 + 1
        //const int numtaps = (int)(data->p[P_DIFFUSION] * (MAXTAPS - 2) + 1); // diff = 0.5 * 1020 + 1 = 611

        //data->random.Seed(0);

        //for (int c = 0; c < 2; c++) {
        //    InstanceChannel& ch = data->ch[c];
        //    const InstanceChannel::Tap* tap_end = ch.taps + numtaps;

        //    float decay = powf(0.01f, 1.0f / (float)numtaps); // 100^(- 1 / numtaps)
        //    float p = 0.0f, amp = (decay - 1.0f) / (powf(decay, numtaps + 1.0f) - 1.0f);
        //    InstanceChannel::Tap* tap = ch.taps;
        //    while (tap != tap_end) {
        //        p += data->random.GetFloat(0.0f, 100.0f);
        //        tap->pos = (int)p;
        //        tap->amp = amp;
        //        amp *= decay;
        //        ++tap;
        //    }

        //    float scale = delaytime / p;
        //    tap = ch.taps;
        //    while (tap != tap_end) {
        //        tap->pos *= (int)scale;
        //        ++tap;
        //    }

        //    for (unsigned int n = 0; n < length; n++) {
        //        ch.delay.Write(inbuffer[n * 2 + c] + reverbmixbuffer[n * 2 + c]);

        //        float s = 0.0f;
        //        const InstanceChannel::Tap* tap = ch.taps;
        //        while (tap != tap_end) {
        //            s += ch.delay.Read(tap->pos) * tap->amp;
        //            ++tap;
        //        }

        //        outbuffer[n * 2 + c] = s;
        //    }
        //}

        //memset(reverbmixbuffer, 0, sizeof(reverbmixbuffer));
        return UNITY_AUDIODSP_OK;
    }


    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK SetFloatParameterCallback(UnityAudioEffectState* state, int index, float value)
    {
        EffectData* data = state->GetEffectData<EffectData>();
        if (index >= P_NUM)
            return UNITY_AUDIODSP_ERR_UNSUPPORTED;
        data->p[index] = value;
        return UNITY_AUDIODSP_OK;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK GetFloatParameterCallback(UnityAudioEffectState* state, int index, float* value, char* valuestr)
    {
        EffectData* data = state->GetEffectData<EffectData>();
        if (index >= P_NUM)
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