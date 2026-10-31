#pragma once
#include "PluginProcessor.h"

class RackLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RackLookAndFeel();
    juce::Font getLabelFont (juce::Label&) override { return juce::Font (juce::FontOptions (12.5f)); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (14.0f)); }
    juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions (14.0f)); }
};

// A rotary knob with a caption. The caption, value text and colour change with the effect in the slot.
class FxKnob : public juce::Component
{
public:
    FxKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption = {});
    void setCaption (const juce::String& text);
    void setTextFunction (std::function<juce::String (float)> fn);
    void setAccent (juce::Colour c);
    void setResetValue (float v);
    void resized() override;

private:
    juce::Label label;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class SlotView : public juce::Component
{
public:
    SlotView (FxRackProcessor& proc, int index);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void applyType (int type);
    void userChoseType (int type);

    FxRackProcessor& processor;
    int idx;
    int currentType = 0;

    juce::ComboBox typeBox;
    juce::ToggleButton onButton { "On" };
    juce::ArrowButton leftButton { "left", 0.5f, juce::Colour (0xffaab3c5) };
    juce::ArrowButton rightButton { "right", 0.0f, juce::Colour (0xffaab3c5) };
    juce::Label emptyLabel;
    std::vector<std::unique_ptr<FxKnob>> knobs; // p1..p4, mix

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;
    std::unique_ptr<juce::ParameterAttachment> typeAttachment;
};

class FxRackEditor : public juce::AudioProcessorEditor
{
public:
    explicit FxRackEditor (FxRackProcessor&);
    ~FxRackEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    RackLookAndFeel laf;
    std::vector<std::unique_ptr<SlotView>> slotViews;
    FxKnob inKnob, outKnob;
    juce::Rectangle<int> flowRow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxRackEditor)
};
