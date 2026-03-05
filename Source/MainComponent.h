#pragma once
#include <JuceHeader.h>

class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // --- Mode selector ---
    juce::Label modeLabel;
    juce::TextButton saturationModeButton { "Saturation" };
    juce::TextButton compressorModeButton { "Compressor" };

    // --- Routing selector ---
    juce::Label routingLabel;
    juce::Label sendLabel       { {}, "Send Output Pair:" };
    juce::Label returnLabel     { {}, "Return Input Pair:" };
    juce::Label monitorLabel    { {}, "Monitor Output Pair:" };
    juce::ComboBox sendCombo;
    juce::ComboBox returnCombo;
    juce::ComboBox monitorCombo;

    // --- Plan editor shell ---
    juce::Label planLabel;
    juce::ListBox planList;

    // --- Status bar ---
    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
