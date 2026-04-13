#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class HardwareColorEditor : public juce::AudioProcessorEditor,
                             private juce::Button::Listener
{
public:
    explicit HardwareColorEditor (HardwareColorProcessor&);
    ~HardwareColorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void updateModelLabel();

    HardwareColorProcessor& processor;

    // --- Knobs ---
    juce::Slider driveSlider  { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider weightSlider { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider mixSlider    { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };

    juce::Label driveLabel  { {}, "Drive" };
    juce::Label weightLabel { {}, "Weight" };
    juce::Label mixLabel    { {}, "Mix" };

    // APVTS attachments keep sliders in sync with parameters.
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> weightAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttach;

    // --- Artifact loader ---
    juce::TextButton loadButton { "Load Model..." };
    juce::Label      modelLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HardwareColorEditor)
};
