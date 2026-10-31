#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
}

void FxRackProcessor::Slot::prepare (double sr, int maxBlock, const SlotParams& init)
{
    for (int t = 1; t < tri::fxCount; ++t)
    {
        fx[(size_t) t] = tri::createEffect (t);
        fx[(size_t) t]->prepare (sr, maxBlock);
    }
    dry.setSize (2, maxBlock);
    mixSm.reset (sr, 0.02);
    actSm.reset (sr, 0.02);
    mixSm.setCurrentAndTargetValue (init.mix);
    actSm.setCurrentAndTargetValue ((init.type > 0 && init.on) ? 1.0f : 0.0f);
    lastType = init.type;
    wasActive = init.type > 0 && init.on;
}

void FxRackProcessor::Slot::process (float* l, float* r, int n, const SlotParams& sp)
{
    if (sp.type != lastType)
    {
        if (lastType > 0) fx[(size_t) lastType]->reset();
        if (sp.type > 0)  fx[(size_t) sp.type]->reset();
        lastType = sp.type;
    }

    if (sp.type == 0)
    {
        actSm.setCurrentAndTargetValue (0.0f);
        wasActive = false;
        return;
    }

    const float target = sp.on ? 1.0f : 0.0f;
    actSm.setTargetValue (target);
    mixSm.setTargetValue (sp.mix);

    if (target == 0.0f && ! actSm.isSmoothing() && actSm.getCurrentValue() == 0.0f)
    {
        if (wasActive) fx[(size_t) sp.type]->reset();
        wasActive = false;
        return;
    }
    wasActive = true;

    juce::FloatVectorOperations::copy (dry.getWritePointer (0), l, n);
    juce::FloatVectorOperations::copy (dry.getWritePointer (1), r, n);

    fx[(size_t) sp.type]->process (l, r, n, sp.p);

    const float k = tri::effectDef (sp.type).additiveMix ? 0.0f : 1.0f;
    const float* dl = dry.getReadPointer (0);
    const float* dr = dry.getReadPointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float g = mixSm.getNextValue() * actSm.getNextValue();
        l[i] = dl[i] + g * (l[i] - k * dl[i]);
        r[i] = dr[i] + g * (r[i] - k * dr[i]);
    }
}

FxRackProcessor::FxRackProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", tri::createLayout())
{
}

bool FxRackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

FxRackProcessor::SlotParams FxRackProcessor::readSlot (int s) const
{
    SlotParams sp;
    sp.type = juce::jlimit (0, (int) tri::fxCount - 1, (int) get (tri::slotId (s, "type")));
    sp.on = get (tri::slotId (s, "on")) > 0.5f;
    for (int k = 0; k < 4; ++k) sp.p[k] = get (tri::slotId (s, "p" + juce::String (k + 1)));
    sp.mix = get (tri::slotId (s, "mix"));
    return sp;
}

void FxRackProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = juce::jmax (1, samplesPerBlock);
    for (int s = 0; s < tri::kSlots; ++s)
        slots[(size_t) s].prepare (sampleRate, maxBlock, readSlot (s));

    inSm.reset (sampleRate, 0.02);  inSm.setCurrentAndTargetValue (dbToGain (get ("in_gain")));
    outSm.reset (sampleRate, 0.02); outSm.setCurrentAndTargetValue (dbToGain (get ("out_gain")));
}

void FxRackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || total == 0)
        return;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    SlotParams sp[tri::kSlots];
    for (int s = 0; s < tri::kSlots; ++s) sp[s] = readSlot (s);
    inSm.setTargetValue (dbToGain (get ("in_gain")));
    outSm.setTargetValue (dbToGain (get ("out_gain")));

    for (int pos = 0; pos < total; pos += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - pos);
        float* l = L + pos;
        float* r = R + pos;

        for (int i = 0; i < n; ++i)
        {
            const float g = inSm.getNextValue();
            l[i] *= g; r[i] *= g;
        }

        for (int s = 0; s < tri::kSlots; ++s)
            slots[(size_t) s].process (l, r, n, sp[s]);

        for (int i = 0; i < n; ++i)
        {
            const float g = outSm.getNextValue();
            l[i] *= g; r[i] *= g;
        }
    }
}

void FxRackProcessor::swapSlots (int a, int b)
{
    if (a == b || a < 0 || b < 0 || a >= tri::kSlots || b >= tri::kSlots)
        return;

    const char* names[] = { "type", "on", "p1", "p2", "p3", "p4", "mix" };
    for (auto* name : names)
    {
        auto* pa = apvts.getParameter (tri::slotId (a, name));
        auto* pb = apvts.getParameter (tri::slotId (b, name));
        const float va = pa->getValue(), vb = pb->getValue();
        pa->beginChangeGesture(); pb->beginChangeGesture();
        pa->setValueNotifyingHost (vb);
        pb->setValueNotifyingHost (va);
        pa->endChangeGesture(); pb->endChangeGesture();
    }
}

juce::AudioProcessorEditor* FxRackProcessor::createEditor()
{
    return new FxRackEditor (*this);
}

void FxRackProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void FxRackProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FxRackProcessor();
}
