#pragma once
#include "PluginProcessor.h"

class BassEditor : public juce::AudioProcessorEditor
{
public:
    explicit BassEditor (BassProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob   { juce::Slider s; juce::Label l; std::unique_ptr<SA> a; };
    struct Toggle { juce::ToggleButton b; std::unique_ptr<BA> a; };
    struct Combo  { juce::ComboBox c; juce::Label l; std::unique_ptr<CA> a; };

    void addKnob   (int row, const juce::String& id, const juce::String& label);
    void addToggle (int row, const juce::String& id, const juce::String& label);
    void addCombo  (int row, const juce::String& id, const juce::String& label);

    BassProcessor& proc;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<Toggle> toggles;
    juce::OwnedArray<Combo> combos;
    std::vector<std::vector<juce::Component*>> rows { 5 };
    juce::StringArray titles { "OSCILLATORS", "FILTER", "TONE", "KICK DUCK", "ENVELOPE / OUTPUT" };
};
