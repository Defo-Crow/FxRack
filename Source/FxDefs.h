#pragma once
#include "Common.h"
#include <array>
#include <functional>

namespace tri
{
enum FxType { fxEmpty = 0, fxLeslie, fxOctaver, fxDelay, fxReverb, fxChorus, fxVibrato, fxFilter, fxDistortion, fxCount };
constexpr int kSlots = 4;

inline juce::StringArray effectNames()
{
    return { "Empty", "Leslie", "Octaver", "Delay", "Reverb", "Chorus", "Vibrato", "Filter", "Distortion" };
}

// Mapping from the normalised 0..1 knob value to real units. Used by the DSP and by the editor's value text.
namespace fxmap
{
    inline float leslieHornHz (float p)  { return 0.6f * std::pow (12.0f, p); }
    inline float octToneHz (float p)     { return 1000.0f * std::pow (12.0f, p); }
    inline float octGrainMs (float p)    { return 30.0f + 90.0f * p; }
    inline float delayMs (float p)       { return 20.0f * std::pow (50.0f, p); }
    inline float delayFb (float p)       { return 0.95f * p; }
    inline float delayToneHz (float p)   { return 800.0f * std::pow (15.0f, p); }
    inline float reverbPreMs (float p)   { return 100.0f * p; }
    inline float chorusHz (float p)      { return 0.1f * std::pow (50.0f, p); }
    inline float chorusDepthMs (float p) { return 4.0f * p; }
    inline float chorusDelayMs (float p) { return 5.0f + 20.0f * p; }
    inline float vibratoHz (float p)     { return 0.5f * std::pow (20.0f, p); }
    inline float vibratoDepthMs (float p){ return 3.0f * p * p; }
    inline float filterHz (float p)      { return 20.0f * std::pow (1000.0f, p); }
    inline float filterQ (float p)       { return 0.7f + 12.0f * p * p; }
    inline int   filterType (float p)    { return juce::jmin (2, (int) (p * 3.0f)); }
    inline float distGainDb (float p)    { return 40.0f * p; }
    inline float distToneHz (float p)    { return 600.0f * std::pow (25.0f, p); }
    inline float distLevelDb (float p)   { return -24.0f + 30.0f * p; }
}

inline juce::String pctText (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
inline juce::String hzText (float f)
{
    if (f < 10.0f)   return juce::String (f, 2) + " Hz";
    if (f < 1000.0f) return juce::String (juce::roundToInt (f)) + " Hz";
    return juce::String (f / 1000.0f, 2) + " kHz";
}
inline juce::String msText (float ms)
{
    return ms < 10.0f ? juce::String (ms, 1) + " ms" : juce::String (juce::roundToInt (ms)) + " ms";
}
inline juce::String dbText (float db)
{
    return juce::String (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
}

struct ParamDef
{
    juce::String name;
    std::function<juce::String (float)> text;
    float def = 0.5f;
};

struct EffectDef
{
    juce::String name;
    juce::Colour colour;
    ParamDef p[4];
    float defMix = 1.0f;
    bool additiveMix = false; // true: dry stays at full level, wet is added
};

inline std::array<EffectDef, fxCount> makeDefs()
{
    using juce::String;
    std::array<EffectDef, fxCount> d;

    auto setup = [&] (int t, const char* name, juce::uint32 col, float defMix, bool additive,
                      ParamDef a, ParamDef b, ParamDef c, ParamDef e)
    {
        d[(size_t) t].name = name;
        d[(size_t) t].colour = juce::Colour (col);
        d[(size_t) t].defMix = defMix;
        d[(size_t) t].additiveMix = additive;
        d[(size_t) t].p[0] = std::move (a);
        d[(size_t) t].p[1] = std::move (b);
        d[(size_t) t].p[2] = std::move (c);
        d[(size_t) t].p[3] = std::move (e);
    };

    auto none = ParamDef { "", [] (float) { return String(); }, 0.5f };
    setup (fxEmpty, "Empty", 0xff7d879a, 1.0f, false, none, none, none, none);

    setup (fxLeslie, "Leslie", 0xffe5a35b, 1.0f, false,
        { "Speed",   [] (float p) { return hzText (fxmap::leslieHornHz (p)); }, 0.30f },
        { "Depth",   pctText, 0.70f },
        { "Balance", [] (float p) { return p < 0.45f ? "Drum " + pctText (1.0f - p) : p > 0.55f ? "Horn " + pctText (p) : String ("Even"); }, 0.50f },
        { "Spread",  pctText, 0.60f });

    setup (fxOctaver, "Octaver", 0xffb48cf2, 1.0f, true,
        { "Oct down", pctText, 0.70f },
        { "Oct up",   pctText, 0.30f },
        { "Tone",     [] (float p) { return hzText (fxmap::octToneHz (p)); }, 0.60f },
        { "Grain",    [] (float p) { return msText (fxmap::octGrainMs (p)); }, 0.35f });

    setup (fxDelay, "Delay", 0xff5bb6e5, 0.30f, true,
        { "Time",     [] (float p) { return msText (fxmap::delayMs (p)); }, 0.73f },
        { "Feedback", [] (float p) { return pctText (fxmap::delayFb (p)); }, 0.42f },
        { "Tone",     [] (float p) { return hzText (fxmap::delayToneHz (p)); }, 0.60f },
        { "Ping-pong", pctText, 0.60f });

    setup (fxReverb, "Reverb", 0xff6fd3b0, 0.30f, false,
        { "Size",      pctText, 0.60f },
        { "Damping",   pctText, 0.50f },
        { "Width",     pctText, 1.00f },
        { "Pre-delay", [] (float p) { return msText (fxmap::reverbPreMs (p)); }, 0.10f });

    setup (fxChorus, "Chorus", 0xff8fd35b, 0.50f, false,
        { "Rate",   [] (float p) { return hzText (fxmap::chorusHz (p)); }, 0.50f },
        { "Depth",  pctText, 0.50f },
        { "Spread", pctText, 0.70f },
        { "Delay",  [] (float p) { return msText (fxmap::chorusDelayMs (p)); }, 0.30f });

    setup (fxVibrato, "Vibrato", 0xfff27fa5, 1.0f, false,
        { "Rate",   [] (float p) { return hzText (fxmap::vibratoHz (p)); }, 0.75f },
        { "Depth",  pctText, 0.35f },
        { "Shape",  [] (float p) { return p < 0.05f ? String ("Sine") : p > 0.95f ? String ("Triangle") : pctText (p) + " tri"; }, 0.00f },
        { "Spread", pctText, 0.00f });

    setup (fxFilter, "Filter", 0xfff2d45b, 1.0f, false,
        { "Cutoff", [] (float p) { return hzText (fxmap::filterHz (p)); }, 0.55f },
        { "Reso",   pctText, 0.30f },
        { "Type",   [] (float p) { const char* n[] = { "Low-pass", "High-pass", "Band-pass" }; return String (n[fxmap::filterType (p)]); }, 0.00f },
        { "Env",    pctText, 0.00f });

    setup (fxDistortion, "Distortion", 0xffee6a5b, 1.0f, false,
        { "Drive", [] (float p) { return dbText (fxmap::distGainDb (p)); }, 0.40f },
        { "Tone",  [] (float p) { return hzText (fxmap::distToneHz (p)); }, 0.60f },
        { "Shape", [] (float p) { return p < 0.17f ? String ("Soft") : p < 0.42f ? String ("Soft/Hard") : p < 0.58f ? String ("Hard")
                                           : p < 0.83f ? String ("Hard/Fold") : String ("Fold"); }, 0.10f },
        { "Level", [] (float p) { return dbText (fxmap::distLevelDb (p)); }, 0.70f });

    return d;
}

inline const EffectDef& effectDef (int type)
{
    static const std::array<EffectDef, fxCount> defs = makeDefs();
    return defs[(size_t) juce::jlimit (0, (int) fxCount - 1, type)];
}

inline juce::String slotId (int slot, const juce::String& name)
{
    return "s" + juce::String (slot + 1) + "_" + name;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; };

    for (int s = 0; s < kSlots; ++s)
    {
        const String n = "Slot " + String (s + 1) + " ";
        layout.add (std::make_unique<AudioParameterChoice> (pid (slotId (s, "type")), n + "Effect", effectNames(), 0));
        layout.add (std::make_unique<AudioParameterBool> (pid (slotId (s, "on")), n + "On", true));
        for (int k = 1; k <= 4; ++k)
            layout.add (std::make_unique<AudioParameterFloat> (pid (slotId (s, "p" + String (k))), n + "Param " + String (k),
                NormalisableRange<float> (0.0f, 1.0f), 0.5f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (pid (slotId (s, "mix")), n + "Mix",
            NormalisableRange<float> (0.0f, 1.0f), 1.0f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    }

    auto dbTxt = [] (float v, int) { return String (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; };
    layout.add (std::make_unique<AudioParameterFloat> (pid ("in_gain"), "Input",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (dbTxt)));
    layout.add (std::make_unique<AudioParameterFloat> (pid ("out_gain"), "Output",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (dbTxt)));

    return layout;
}
} // namespace tri
