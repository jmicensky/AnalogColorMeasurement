#pragma once
#include <JuceHeader.h>
#include "AudioEngine.h"
#include "LatencyAligner.h"
#include "SessionWriter.h"
#include "StimulusPlan.h"
#include "SpectrumDisplay.h"

class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // --- Plan list model ---
    struct PlanListModel : public juce::ListBoxModel
    {
        const StimulusPlan* plan { nullptr };

        int getNumRows() override
        {
            return plan ? plan->totalSteps() : 0;
        }

        void paintListBoxItem (int row, juce::Graphics& g,
                               int width, int height, bool /*selected*/) override
        {
            if (plan == nullptr || row >= plan->totalSteps())
                return;

            const auto& step     = plan->getSteps()[row];
            const bool isCurrent = (row == plan->currentStepIndex()) && ! plan->isComplete();

            if (isCurrent)
                g.fillAll (juce::Colour (0xff2a5a8a));
            else if (step.completed)
                g.fillAll (juce::Colour (0xff2a4a2a));
            else
                g.fillAll (juce::Colour (0xff222232));

            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));

            const juce::String text = juce::String (row + 1) + ".  "
                                    + step.gainLabel + "  —  "
                                    + step.stimulusName
                                    + (step.completed ? "  [done]" : "");
            g.drawText (text, 8, 0, width - 8, height, juce::Justification::centredLeft);
        }
    };

    void populateRoutingCombos();
    void populateProjectCombo();
    void checkRoutingSafety (juce::ComboBox* changed);
    void onProjectComboChanged();
    void onInitProjectClicked();
    void onBuildPlanClicked();
    juce::String readModeFromSessionJson (const juce::File& folder) const;
    void onMeasureButtonClicked();
    void onRefFileSelected();
    void onAudioSettingsClicked();
    void onAnalyseClicked();
    void timerCallback() override;

    // Loads a named stimulus into AudioEngine from BinaryData.
    // Returns an error string on failure, empty string on success.
    juce::String loadStimulusByName (const juce::String& name);

    // --- Reference file selector ---
    juce::Label    refFileLabel  { {}, "Reference File:" };
    juce::ComboBox refFileCombo;

    // --- Project setup ---
    juce::Label      projectLabel  { {}, "Project:" };
    juce::ComboBox   projectCombo;
    juce::TextEditor projectNameEditor;
    juce::TextButton initProjectButton { "Create Project" };

    // --- Mode selector ---
    juce::Label modeLabel;
    juce::TextButton saturationModeButton { "Saturation" };
    juce::TextButton compressorModeButton { "Compressor" };

    // --- Capture quality selector ---
    juce::Label      qualityLabel  { {}, "Capture Quality:" };
    juce::TextButton quickButton    { "Quick" };
    juce::TextButton standardButton { "Standard" };
    juce::TextButton hifiButton     { "Hi-Fi" };
    CaptureQuality   captureQuality { CaptureQuality::Standard };

    // --- Routing selector ---
    juce::Label      routingLabel;
    juce::TextButton audioSettingsButton { "Audio Settings..." };
    juce::Label sendLabel       { {}, "Send Output Pair:" };
    juce::Label returnLabel     { {}, "Return Input Pair:" };
    juce::Label monitorLabel    { {}, "Monitor Output Pair:" };
    juce::ComboBox sendCombo;
    juce::ComboBox returnCombo;
    juce::ComboBox monitorCombo;
    juce::ToggleButton monoModeToggle { "Mono device (single channel)" };

    // --- Measurement control ---
    juce::TextButton measureButton  { "Start Measurement" };
    juce::TextButton analyseButton  { "Analyze & Fit Model" };

    // --- Plan editor ---
    juce::Label      planLabel;
    juce::Label      gainLabelsLabel { {}, "Gain Levels (csv):" };
    juce::TextEditor gainLabelsEditor;
    juce::TextButton buildPlanButton { "Build Plan" };
    PlanListModel    planListModel;
    juce::ListBox    planList;

    // --- Status bar ---
    juce::Label inputMeterLabel;

    struct MeterTimer : juce::Timer {
        MainComponent* owner;
        void timerCallback() override;
    } meterTimer;

    juce::Label statusLabel;

    // --- Spectrum display ---
    SpectrumDisplay spectrumDisplay;

    // --- Data ---
    SessionWriter            sessionWriter;
    AudioEngine              audioEngine;
    StimulusPlan             stimulusPlan;
    juce::Array<MarkerEntry> markers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
