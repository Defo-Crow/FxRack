#pragma once
#include "Common.h"
#include "FxDefs.h"
#include "Effects.h"

class FxRackProcessor : public juce::AudioProcessor
{
public:
    FxRackProcessor();
    ~FxRackProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FxRack"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override;
    void setStateInformation (const void* data, int size) override;

    // Exchange everything stored in two slots (effect, on/off, knobs, mix). Call from the message thread.
    void swapSlots (int a, int b);

    juce::AudioProcessorValueTreeState apvts;

private:
    struct SlotParams { int type = 0; bool on = true; float p[4] {}; float mix = 1.0f; };

    struct Slot
    {
        std::array<std::unique_ptr<tri::Effect>, tri::fxCount> fx;
        juce::AudioBuffer<float> dry;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSm, actSm;
        int lastType = 0;
        bool wasActive = false;

        void prepare (double sr, int maxBlock, const SlotParams& init);
        void process (float* l, float* r, int n, const SlotParams& sp);
    };

    float get (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    SlotParams readSlot (int s) const;

    std::array<Slot, tri::kSlots> slots;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inSm, outSm;
    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxRackProcessor)
};
