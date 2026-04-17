#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "../Source/SpectrumDisplay.h"

class HardwareColorEditor : public juce::AudioProcessorEditor,
                             private juce::Button::Listener,
                             private juce::Timer
{
public:
    explicit HardwareColorEditor (HardwareColorProcessor&);
    ~HardwareColorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void timerCallback() override;
    void updateModelLabel();

    bool  lastModelState { false };
    float lastWeight     { -1.0f };  // force update on first tick

    void updateSpectrumDisplay();   // applies current weight tilt to stored FR

    HardwareColorProcessor& processor;

    // --- Knobs ---
    juce::Slider driveSlider     { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider weightSlider    { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider mixSlider       { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider outputGainSlider{ juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };

    juce::Label driveLabel     { {}, "Drive" };
    juce::Label weightLabel    { {}, "Weight" };
    juce::Label mixLabel       { {}, "Mix" };
    juce::Label outputGainLabel{ {}, "Output" };

    // APVTS attachments keep sliders in sync with parameters.
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> weightAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttach;

    // --- Bypass ---
    juce::ToggleButton bypassButton { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach;

    // --- Artifact loader ---
    juce::TextButton  loadButton { "Load Model..." };
    juce::Label       modelLabel;

    // --- Spectrum display ---
    SpectrumDisplay spectrumDisplay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HardwareColorEditor)
};
