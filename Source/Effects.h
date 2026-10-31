#pragma once
#include "Common.h"
#include "FxDefs.h"

namespace tri
{
// Each effect gets the incoming stereo signal in place and replaces it with the processed ("wet") signal.
// Dry/wet mixing, bypass and ordering are handled by the processor.
class Effect
{
public:
    virtual ~Effect() = default;
    virtual void prepare (double sampleRate, int maxBlock) = 0;
    virtual void reset() = 0;
    virtual void process (float* l, float* r, int n, const float* p) = 0; // p[0..3] normalised 0..1
};

constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

class DelayLine
{
public:
    void prepare (int maxSamples) { size = maxSamples + 4; buf.assign ((size_t) size, 0.0f); wp = 0; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); wp = 0; }
    int getSize() const noexcept { return size; }

    void push (float x) noexcept
    {
        buf[(size_t) wp] = x;
        if (++wp >= size) wp = 0;
    }

    // d = 0 returns the most recently pushed sample
    float read (float d) const noexcept
    {
        float rp = (float) wp - 1.0f - d;
        while (rp < 0.0f) rp += (float) size;
        int i0 = (int) rp;
        const float fr = rp - (float) i0;
        if (i0 >= size) i0 -= size;
        int i1 = i0 + 1;
        if (i1 >= size) i1 = 0;
        return buf[(size_t) i0] * (1.0f - fr) + buf[(size_t) i1] * fr;
    }

private:
    std::vector<float> buf;
    int size = 4, wp = 0;
};

inline float onePoleCoef (float hz, double sr) { return 1.0f - std::exp (-kTwoPi * hz / (float) sr); }

//==============================================================================
class LeslieFx : public Effect
{
public:
    void prepare (double sr, int maxBlock) override
    {
        sampleRate = (float) sr;
        xover.prepare ({ sr, (juce::uint32) maxBlock, 1 });
        xover.setCutoffFrequency (800.0f);
        horn.prepare (4096);
        drum.prepare (4096);
        hornAlpha = 1.0f - std::exp (-1.0f / (0.5f * sampleRate));
        drumAlpha = 1.0f - std::exp (-1.0f / (1.5f * sampleRate));
        reset();
    }

    void reset() override
    {
        xover.reset(); horn.reset(); drum.reset();
        hornPh = 0.0f; drumPh = 0.0f;
        hornHz = drumHz = -1.0f; // snap to target on first sample
    }

    void process (float* l, float* r, int n, const float* p) override
    {
        const float targetHorn = fxmap::leslieHornHz (p[0]);
        const float targetDrum = targetHorn * 0.85f;
        if (hornHz < 0.0f) { hornHz = targetHorn; drumHz = targetDrum; }
        const float depth = p[1];
        const float hornG = juce::jlimit (0.0f, 1.6f, 2.0f * p[2]);
        const float drumG = juce::jlimit (0.0f, 1.6f, 2.0f * (1.0f - p[2]));
        const float off = p[3] * 0.5f;
        const float hornDm = 0.0005f * sampleRate * depth;
        const float drumDm = 0.0009f * sampleRate * depth;

        auto tap = [] (const DelayLine& line, float ph, float dm, float am, float depthAmt)
        {
            const float s = std::sin (kTwoPi * ph), c = std::cos (kTwoPi * ph);
            return line.read (dm * (1.0f + s) + 1.0f) * (1.0f + am * depthAmt * c);
        };

        for (int i = 0; i < n; ++i)
        {
            hornHz += (targetHorn - hornHz) * hornAlpha;
            drumHz += (targetDrum - drumHz) * drumAlpha;

            float lo = 0.0f, hi = 0.0f;
            xover.processSample (0, 0.5f * (l[i] + r[i]), lo, hi);
            horn.push (hi);
            drum.push (lo);

            const float hl = tap (horn, hornPh, hornDm, 0.5f, depth);
            const float hr = tap (horn, hornPh + off, hornDm, 0.5f, depth);
            const float dl = tap (drum, drumPh, drumDm, 0.35f, depth);
            const float dr = tap (drum, drumPh + off, drumDm, 0.35f, depth);

            l[i] = 0.85f * (hl * hornG + dl * drumG);
            r[i] = 0.85f * (hr * hornG + dr * drumG);

            hornPh += hornHz / sampleRate; if (hornPh >= 1.0f) hornPh -= 1.0f;
            drumPh += drumHz / sampleRate; if (drumPh >= 1.0f) drumPh -= 1.0f;
        }
    }

private:
    float sampleRate = 44100.0f;
    juce::dsp::LinkwitzRileyFilter<float> xover;
    DelayLine horn, drum;
    float hornPh = 0, drumPh = 0, hornHz = -1, drumHz = -1, hornAlpha = 0, drumAlpha = 0;
};

//==============================================================================
class OctaverFx : public Effect
{
public:
    void prepare (double sr, int) override
    {
        sampleRate = sr;
        for (int c = 0; c < 2; ++c) { down[c].line.prepare ((int) (0.2 * sr)); up[c].line.prepare ((int) (0.2 * sr)); }
        reset();
    }

    void reset() override
    {
        for (int c = 0; c < 2; ++c) { down[c].line.reset(); up[c].line.reset(); down[c].phase = 0.0f; up[c].phase = 0.5f; lp1[c] = lp2[c] = 0.0f; }
    }

    void process (float* l, float* r, int n, const float* p) override
    {
        const float lvDown = p[0], lvUp = p[1];
        const float a = onePoleCoef (fxmap::octToneHz (p[2]), sampleRate);
        const float W = fxmap::octGrainMs (p[3]) * 0.001f * (float) sampleRate;
        float* ch[2] = { l, r };

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float x = ch[c][i];
                float wet = down[c].process (x, 0.5f, W) * lvDown + up[c].process (x, 2.0f, W) * lvUp;
                lp1[c] += a * (wet - lp1[c]);
                lp2[c] += a * (lp1[c] - lp2[c]);
                ch[c][i] = lp2[c];
            }
    }

private:
    struct Shifter
    {
        DelayLine line;
        float phase = 0.0f;

        float process (float x, float ratio, float W)
        {
            line.push (x);
            float pB = phase + 0.5f;
            if (pB >= 1.0f) pB -= 1.0f;
            const float a = line.read (phase * W), b = line.read (pB * W);
            const float wA = 0.5f - 0.5f * std::cos (kTwoPi * phase);
            phase += (1.0f - ratio) / W;
            if (phase < 0.0f) phase += 1.0f;
            if (phase >= 1.0f) phase -= 1.0f;
            return a * wA + b * (1.0f - wA);
        }
    };

    double sampleRate = 44100.0;
    Shifter down[2], up[2];
    float lp1[2] { 0, 0 }, lp2[2] { 0, 0 };
};

//==============================================================================
class DelayFx : public Effect
{
public:
    void prepare (double sr, int) override
    {
        sampleRate = sr;
        lineL.prepare ((int) (sr * 1.1)); lineR.prepare ((int) (sr * 1.1));
        timeSm.reset (sr, 0.08);
        reset();
    }

    void reset() override
    {
        lineL.reset(); lineR.reset(); fbL = fbR = 0.0f;
        timeSm.setCurrentAndTargetValue (350.0f);
        first = true;
    }

    void process (float* l, float* r, int n, const float* p) override
    {
        const float timeMs = fxmap::delayMs (p[0]);
        if (first) { timeSm.setCurrentAndTargetValue (timeMs); first = false; }
        timeSm.setTargetValue (timeMs);
        const float fb = fxmap::delayFb (p[1]);
        const float a = std::exp (-kTwoPi * fxmap::delayToneHz (p[2]) / (float) sampleRate);
        const float pp = p[3];
        const float maxT = (float) lineL.getSize() - 6.0f;

        for (int i = 0; i < n; ++i)
        {
            const float t = juce::jlimit (2.0f, maxT, timeSm.getNextValue() * 0.001f * (float) sampleRate);
            const float dl = lineL.read (t - 1.0f), dr = lineR.read (t - 1.0f);
            fbL = (1.0f - a) * dl + a * fbL;
            fbR = (1.0f - a) * dr + a * fbR;

            const float inL = l[i], inR = r[i], m = 0.5f * (inL + inR);
            const float xL = inL * (1.0f - pp) + m * pp, xR = inR * (1.0f - pp);
            lineL.push (xL + fb * (fbL * (1.0f - pp) + fbR * pp));
            lineR.push (xR + fb * (fbR * (1.0f - pp) + fbL * pp));

            l[i] = dl; r[i] = dr;
        }
    }

private:
    double sampleRate = 44100.0;
    DelayLine lineL, lineR;
    float fbL = 0, fbR = 0;
    bool first = true;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> timeSm;
};

//==============================================================================
class ReverbFx : public Effect
{
public:
    void prepare (double sr, int) override
    {
        sampleRate = sr;
        reverb.setSampleRate (sr);
        preL.prepare ((int) (0.12 * sr)); preR.prepare ((int) (0.12 * sr));
        preSm.reset (sr, 0.05);
        reset();
    }

    void reset() override { reverb.reset(); preL.reset(); preR.reset(); preSm.setCurrentAndTargetValue (0.0f); first = true; }

    void process (float* l, float* r, int n, const float* p) override
    {
        juce::Reverb::Parameters rp;
        rp.roomSize = p[0]; rp.damping = p[1]; rp.width = p[2];
        rp.wetLevel = 0.33f; rp.dryLevel = 0.0f; rp.freezeMode = 0.0f;
        reverb.setParameters (rp);

        const float pre = fxmap::reverbPreMs (p[3]) * 0.001f * (float) sampleRate;
        if (first) { preSm.setCurrentAndTargetValue (pre); first = false; }
        preSm.setTargetValue (pre);
        for (int i = 0; i < n; ++i)
        {
            const float d = preSm.getNextValue();
            preL.push (l[i]); preR.push (r[i]);
            l[i] = preL.read (d); r[i] = preR.read (d);
        }
        reverb.processStereo (l, r, n);
    }

private:
    double sampleRate = 44100.0;
    juce::Reverb reverb;
    DelayLine preL, preR;
    bool first = true;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> preSm;
};

//==============================================================================
class ChorusFx : public Effect
{
public:
    void prepare (double sr, int) override
    {
        sampleRate = (float) sr;
        lines[0].prepare ((int) (0.06 * sr)); lines[1].prepare ((int) (0.06 * sr));
        reset();
    }
    void reset() override { lines[0].reset(); lines[1].reset(); phase = 0.0f; }

    void process (float* l, float* r, int n, const float* p) override
    {
        const float inc = fxmap::chorusHz (p[0]) / sampleRate;
        const float depth = fxmap::chorusDepthMs (p[1]) * 0.001f * sampleRate;
        const float spread = p[2] * 0.5f;
        const float base = fxmap::chorusDelayMs (p[3]) * 0.001f * sampleRate;
        float* ch[2] = { l, r };

        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                lines[c].push (ch[c][i]);
                const float ph = phase + (c == 1 ? spread : 0.0f);
                const float d1 = juce::jmax (0.0f, base + depth * std::sin (kTwoPi * ph));
                const float d2 = juce::jmax (0.0f, base * 1.25f + depth * std::sin (kTwoPi * (ph + 0.37f)));
                ch[c][i] = 0.5f * (lines[c].read (d1) + lines[c].read (d2));
            }
            phase += inc; if (phase >= 1.0f) phase -= 1.0f;
        }
    }

private:
    float sampleRate = 44100.0f, phase = 0.0f;
    DelayLine lines[2];
};

//==============================================================================
class VibratoFx : public Effect
{
public:
    void prepare (double sr, int) override
    {
        sampleRate = (float) sr;
        lines[0].prepare ((int) (0.02 * sr)); lines[1].prepare ((int) (0.02 * sr));
        reset();
    }
    void reset() override { lines[0].reset(); lines[1].reset(); phase = 0.0f; }

    void process (float* l, float* r, int n, const float* p) override
    {
        const float inc = fxmap::vibratoHz (p[0]) / sampleRate;
        const float depthMs = fxmap::vibratoDepthMs (p[1]);
        const float depth = depthMs * 0.001f * sampleRate;
        const float base = depth + 0.0005f * sampleRate;
        const float shape = p[2];
        const float spread = p[3] * 0.5f;
        float* ch[2] = { l, r };

        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                lines[c].push (ch[c][i]);
                const float ph = phase + (c == 1 ? spread : 0.0f);
                const float x = ph - std::floor (ph + 0.5f);
                const float tri = 4.0f * std::abs (x) - 1.0f;
                const float lfo = (1.0f - shape) * std::sin (kTwoPi * ph) + shape * tri;
                ch[c][i] = lines[c].read (juce::jmax (0.0f, base + depth * lfo));
            }
            phase += inc; if (phase >= 1.0f) phase -= 1.0f;
        }
    }

private:
    float sampleRate = 44100.0f, phase = 0.0f;
    DelayLine lines[2];
};

//==============================================================================
class FilterFx : public Effect
{
public:
    void prepare (double sr, int maxBlock) override
    {
        sampleRate = (float) sr;
        filt.prepare ({ sr, (juce::uint32) maxBlock, 2 });
        atk = onePoleCoef (60.0f, sr);   // ~3 ms
        rel = onePoleCoef (1.5f, sr);    // ~100 ms
        reset();
    }
    void reset() override { filt.reset(); env = 0.0f; }

    void process (float* l, float* r, int n, const float* p) override
    {
        const float baseHz = fxmap::filterHz (p[0]);
        const float envAmt = p[3];
        filt.setResonance (fxmap::filterQ (p[1]));
        const int t = fxmap::filterType (p[2]);
        filt.setType (t == 0 ? juce::dsp::StateVariableTPTFilterType::lowpass
                    : t == 1 ? juce::dsp::StateVariableTPTFilterType::highpass
                             : juce::dsp::StateVariableTPTFilterType::bandpass);
        const float limit = 0.45f * sampleRate;

        for (int i = 0; i < n; ++i)
        {
            const float level = juce::jmax (std::abs (l[i]), std::abs (r[i]));
            env += (level > env ? atk : rel) * (level - env);

            if ((i & 15) == 0)
            {
                const float e = juce::jlimit (0.0f, 1.0f, env * 3.0f);
                filt.setCutoffFrequency (juce::jlimit (20.0f, limit, baseHz * std::pow (2.0f, envAmt * 6.0f * e)));
            }
            l[i] = filt.processSample (0, l[i]);
            r[i] = filt.processSample (1, r[i]);
        }
    }

private:
    float sampleRate = 44100.0f, env = 0.0f, atk = 0.1f, rel = 0.001f;
    juce::dsp::StateVariableTPTFilter<float> filt;
};

//==============================================================================
class DistortionFx : public Effect
{
public:
    DistortionFx() : os (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true) {}

    void prepare (double sr, int maxBlock) override
    {
        sampleRate = (float) sr;
        os.initProcessing ((size_t) maxBlock);
        tone.prepare ({ sr, (juce::uint32) maxBlock, 2 });
        tone.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        tone.setResonance (0.7f);
        hpCoef = onePoleCoef (60.0f, sr);
        reset();
    }

    void reset() override { os.reset(); tone.reset(); hp[0] = hp[1] = 0.0f; }

    void process (float* l, float* r, int n, const float* p) override
    {
        const float gain = std::pow (10.0f, fxmap::distGainDb (p[0]) / 20.0f);
        const float shape = p[2] * 2.0f;
        const float level = std::pow (10.0f, fxmap::distLevelDb (p[3]) / 20.0f);
        tone.setCutoffFrequency (juce::jmin (fxmap::distToneHz (p[1]), 0.45f * sampleRate));

        float* ch[2] = { l, r };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                hp[c] += hpCoef * (ch[c][i] - hp[c]);
                ch[c][i] -= hp[c];
            }

        juce::dsp::AudioBlock<float> block (ch, 2, (size_t) n);
        auto up = os.processSamplesUp (block);
        for (size_t c = 0; c < 2; ++c)
        {
            float* d = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                const float x = d[i] * gain;
                const float t = std::tanh (x);
                const float h = juce::jlimit (-1.0f, 1.0f, x);
                const float f = std::sin (x);
                d[i] = shape < 1.0f ? t + (h - t) * shape : h + (f - h) * (shape - 1.0f);
            }
        }
        os.processSamplesDown (block);

        for (int i = 0; i < n; ++i)
        {
            l[i] = tone.processSample (0, l[i]) * level;
            r[i] = tone.processSample (1, r[i]) * level;
        }
    }

private:
    float sampleRate = 44100.0f, hpCoef = 0.01f;
    float hp[2] { 0, 0 };
    juce::dsp::Oversampling<float> os;
    juce::dsp::StateVariableTPTFilter<float> tone;
};

inline std::unique_ptr<Effect> createEffect (int type)
{
    switch (type)
    {
        case fxLeslie:     return std::make_unique<LeslieFx>();
        case fxOctaver:    return std::make_unique<OctaverFx>();
        case fxDelay:      return std::make_unique<DelayFx>();
        case fxReverb:     return std::make_unique<ReverbFx>();
        case fxChorus:     return std::make_unique<ChorusFx>();
        case fxVibrato:    return std::make_unique<VibratoFx>();
        case fxFilter:     return std::make_unique<FilterFx>();
        case fxDistortion: return std::make_unique<DistortionFx>();
        default:           return nullptr;
    }
}
} // namespace tri
