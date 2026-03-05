#pragma once
#include <JuceHeader.h>
#include "AudioEngine.h"
#include "SessionWriter.h"

class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void populateRoutingCombos();
    void checkRoutingSafety (juce::ComboBox* changed);
    void onInitProjectClicked();
    void onMeasureButtonClicked();

    // --- Project setup ---
    juce::Label      projectLabel  { {}, "Project Name:" };
    juce::TextEditor projectNameEditor;
    juce::TextButton initProjectButton { "Initialize Project" };

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

    // --- Measurement control ---
    juce::TextButton measureButton { "Start Measurement" };

    // --- Plan editor shell ---
    juce::Label planLabel;
    juce::ListBox planList;

    // --- Status bar ---
    juce::Label statusLabel;

    // --- Session writer (owns project folder + file paths) ---
    SessionWriter sessionWriter;

    // --- Audio engine (owns AudioDeviceManager) ---
    AudioEngine audioEngine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
