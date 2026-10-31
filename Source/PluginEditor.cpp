#include "PluginEditor.h"

namespace
{
const juce::Colour kBg      { 0xff14161c };
const juce::Colour kPanel   { 0xff1e222b };
const juce::Colour kText    { 0xffd8dee9 };
const juce::Colour kMuted   { 0xff7d879a };
const juce::Colour kOutline { 0xff3a4150 };
}

RackLookAndFeel::RackLookAndFeel()
{
    setColourScheme (getDarkColourScheme());
    setColour (juce::Slider::rotarySliderFillColourId, kMuted);
    setColour (juce::Slider::rotarySliderOutlineColourId, kOutline);
    setColour (juce::Slider::thumbColourId, kText);
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, kText);
    setColour (juce::ComboBox::backgroundColourId, kBg);
    setColour (juce::ComboBox::outlineColourId, kOutline);
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::arrowColourId, kText);
    setColour (juce::ToggleButton::tickColourId, kText);
    setColour (juce::ToggleButton::textColourId, kText);
    setColour (juce::PopupMenu::backgroundColourId, kPanel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kOutline);
}

// ---------------------------------------------------------------- FxKnob

FxKnob::FxKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption)
{
    label.setText (caption, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, kMuted);
    addAndMakeVisible (label);

    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setTextBoxIsEditable (false);
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
}

void FxKnob::setCaption (const juce::String& text) { label.setText (text, juce::dontSendNotification); }

void FxKnob::setTextFunction (std::function<juce::String (float)> fn)
{
    slider.textFromValueFunction = [fn] (double v) { return fn ((float) v); };
    slider.updateText();
}

void FxKnob::setAccent (juce::Colour c) { slider.setColour (juce::Slider::rotarySliderFillColourId, c); }
void FxKnob::setResetValue (float v) { slider.setDoubleClickReturnValue (true, (double) v); }

void FxKnob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    const int w = juce::jmin (r.getWidth(), 92);
    slider.setBounds (r.withSizeKeepingCentre (w, r.getHeight()));
}

// -------------------------------------------------------------- SlotView

SlotView::SlotView (FxRackProcessor& proc, int index)
    : processor (proc), idx (index)
{
    auto& s = proc.apvts;

    typeBox.addItemList (tri::effectNames(), 1);
    typeBox.onChange = [this] { userChoseType (typeBox.getSelectedItemIndex()); };
    addAndMakeVisible (typeBox);

    addAndMakeVisible (onButton);
    onAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, tri::slotId (idx, "on"), onButton);

    leftButton.setVisible (idx > 0);
    rightButton.setVisible (idx < tri::kSlots - 1);
    leftButton.onClick  = [this] { processor.swapSlots (idx, idx - 1); };
    rightButton.onClick = [this] { processor.swapSlots (idx, idx + 1); };
    leftButton.setTooltip ("Move this effect one place earlier in the chain");
    rightButton.setTooltip ("Move this effect one place later in the chain");
    addAndMakeVisible (leftButton);
    addAndMakeVisible (rightButton);

    emptyLabel.setText ("Empty slot\nPick an effect above", juce::dontSendNotification);
    emptyLabel.setJustificationType (juce::Justification::centred);
    emptyLabel.setColour (juce::Label::textColourId, kMuted);
    addChildComponent (emptyLabel);

    for (int k = 1; k <= 4; ++k)
        knobs.push_back (std::make_unique<FxKnob> (s, tri::slotId (idx, "p" + juce::String (k))));
    knobs.push_back (std::make_unique<FxKnob> (s, tri::slotId (idx, "mix"), "Mix"));
    for (auto& k : knobs) addChildComponent (*k);
    knobs.back()->setTextFunction (tri::pctText);

    auto* typeParam = s.getParameter (tri::slotId (idx, "type"));
    typeAttachment = std::make_unique<juce::ParameterAttachment> (*typeParam, [this] (float v) { applyType ((int) v); });
    typeAttachment->sendInitialUpdate();
}

void SlotView::applyType (int type)
{
    currentType = type;
    typeBox.setSelectedItemIndex (type, juce::dontSendNotification);

    const bool empty = type == 0;
    emptyLabel.setVisible (empty);
    onButton.setVisible (! empty);
    for (auto& k : knobs) k->setVisible (! empty);

    if (! empty)
    {
        const auto& def = tri::effectDef (type);
        for (int k = 0; k < 4; ++k)
        {
            knobs[(size_t) k]->setCaption (def.p[k].name);
            knobs[(size_t) k]->setTextFunction (def.p[k].text);
            knobs[(size_t) k]->setResetValue (def.p[k].def);
        }
        knobs[4]->setResetValue (def.defMix);
        for (auto& k : knobs) k->setAccent (def.colour);
    }
    repaint();
}

void SlotView::userChoseType (int type)
{
    typeAttachment->setValueAsCompleteGesture ((float) type);

    if (type > 0)
    {
        const auto& def = tri::effectDef (type);
        auto setParam = [this] (const juce::String& id, float normalised)
        {
            auto* p = processor.apvts.getParameter (id);
            p->beginChangeGesture();
            p->setValueNotifyingHost (normalised);
            p->endChangeGesture();
        };
        for (int k = 0; k < 4; ++k)
            setParam (tri::slotId (idx, "p" + juce::String (k + 1)), def.p[k].def);
        setParam (tri::slotId (idx, "mix"), def.defMix);
        setParam (tri::slotId (idx, "on"), 1.0f);
    }
    applyType (type);
}

void SlotView::paint (juce::Graphics& g)
{
    const auto& def = tri::effectDef (currentType);
    auto b = getLocalBounds().toFloat();
    g.setColour (kPanel);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (kOutline);
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);

    // coloured strip on top, grey when the slot is empty
    g.setColour (def.colour.withAlpha (currentType == 0 ? 0.35f : 1.0f));
    g.fillRect (juce::Rectangle<float> (b.getX() + 6.0f, b.getY() + 1.0f, b.getWidth() - 12.0f, 3.0f));

    g.setColour (currentType == 0 ? kMuted : def.colour);
    g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    g.drawText (juce::String (idx + 1), 12, 10, 24, 28, juce::Justification::centredLeft);
}

void SlotView::resized()
{
    auto area = getLocalBounds().reduced (10, 10);
    area.removeFromTop (4);

    auto header = area.removeFromTop (28);
    header.removeFromLeft (26);
    rightButton.setBounds (header.removeFromRight (26).reduced (4));
    leftButton.setBounds (header.removeFromRight (26).reduced (4));
    onButton.setBounds (header.removeFromLeft (64));

    area.removeFromTop (6);
    typeBox.setBounds (area.removeFromTop (30));
    area.removeFromTop (8);

    emptyLabel.setBounds (area);

    const int rowH = area.getHeight() / 2;
    auto row1 = area.removeFromTop (rowH);
    auto row2 = area;
    const int cellW = row1.getWidth() / 3;
    for (int k = 0; k < 3; ++k)
        knobs[(size_t) k]->setBounds (row1.removeFromLeft (cellW));
    row2.removeFromLeft (cellW / 2);
    knobs[3]->setBounds (row2.removeFromLeft (cellW));
    knobs[4]->setBounds (row2.removeFromLeft (cellW));
}

// ---------------------------------------------------------------- Editor

FxRackEditor::FxRackEditor (FxRackProcessor& p)
    : AudioProcessorEditor (&p),
      inKnob (p.apvts, "in_gain", "Input"),
      outKnob (p.apvts, "out_gain", "Output")
{
    setLookAndFeel (&laf);

    for (int i = 0; i < tri::kSlots; ++i)
    {
        slotViews.push_back (std::make_unique<SlotView> (p, i));
        addAndMakeVisible (*slotViews.back());
    }

    inKnob.setResetValue (0.0f);  outKnob.setResetValue (0.0f);
    inKnob.setAccent (kText);     outKnob.setAccent (kText);
    inKnob.setTextFunction (tri::dbText);
    outKnob.setTextFunction (tri::dbText);
    addAndMakeVisible (inKnob);
    addAndMakeVisible (outKnob);

    setSize (1240, 500);
}

FxRackEditor::~FxRackEditor()
{
    setLookAndFeel (nullptr);
}

void FxRackEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kText);
    g.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
    g.drawText ("FX RACK", 16, 10, 220, 34, juce::Justification::left);
    g.setColour (kMuted);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText ("The signal passes through the slots from left to right", getWidth() - 456, 18, 440, 20, juce::Justification::right);

    // arrows between the stages: input -> slot 1 -> ... -> slot 4 -> output
    g.setColour (kMuted);
    const float cy = (float) flowRow.getCentreY();
    auto arrow = [&] (float x)
    {
        juce::Path a;
        a.addTriangle (x - 4.0f, cy - 5.0f, x - 4.0f, cy + 5.0f, x + 5.0f, cy);
        g.fillPath (a);
    };
    for (int i = 0; i < tri::kSlots; ++i)
    {
        const auto b = slotViews[(size_t) i]->getBounds();
        arrow ((float) b.getX() - 9.0f);
    }
    arrow ((float) slotViews.back()->getRight() + 9.0f);
}

void FxRackEditor::resized()
{
    auto r = getLocalBounds().reduced (16, 0);
    r.removeFromTop (56);
    r.removeFromBottom (16);
    flowRow = r;

    auto inArea = r.removeFromLeft (84);
    auto outArea = r.removeFromRight (84);
    inKnob.setBounds (inArea.withSizeKeepingCentre (inArea.getWidth(), 120));
    outKnob.setBounds (outArea.withSizeKeepingCentre (outArea.getWidth(), 120));

    r.removeFromLeft (20);
    r.removeFromRight (20);
    const int gap = 20;
    const int w = (r.getWidth() - gap * (tri::kSlots - 1)) / tri::kSlots;
    for (int i = 0; i < tri::kSlots; ++i)
    {
        slotViews[(size_t) i]->setBounds (r.removeFromLeft (w));
        r.removeFromLeft (gap);
    }
}
