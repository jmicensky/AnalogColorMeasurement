#pragma once
#include <JuceHeader.h>
#include "CRTLookAndFeel.h"
#include "AudioEngine.h"
#include "LatencyAligner.h"
#include "SessionWriter.h"
#include "StimulusPlan.h"
#include "SpectrumDisplay.h"
#include "LevelMeter.h"

class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // Declared first so it outlives all child component members.
    CRTLookAndFeel crtLookAndFeel;
    int            sidebarWidth { 470 };  // updated in resized(), read in paint()

    // --- Plan list model ---
    struct PlanListModel : public juce::ListBoxModel
    {
        const StimulusPlan* plan  { nullptr };
        float               scale { 1.0f };

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
                g.fillAll (juce::Colour (0xff0d2a0d));
            else if (step.completed)
                g.fillAll (juce::Colour (0xff0a1a0a));
            else
                g.fillAll (juce::Colour (CRTLookAndFeel::kColBgBase));

            if (isCurrent)
                g.setColour (juce::Colour (CRTLookAndFeel::kColGreenPrimary));
            else if (step.completed)
                g.setColour (juce::Colour (CRTLookAndFeel::kColGreenDim));
            else
                g.setColour (juce::Colour (0x4433ff33));

            g.setFont (juce::Font (juce::FontOptions()
                           .withName ("Courier New")
                           .withHeight (13.0f * scale)));

            const int indent = juce::roundToInt (8 * scale);
            const juce::String text = juce::String (row + 1) + ".  "
                                    + step.gainLabel + "  \xe2\x80\x94  "
                                    + step.stimulusName
                                    + (step.completed ? "  [done]" : "");
            g.drawText (text, indent, 0, width - indent, height, juce::Justification::centredLeft);
        }
    };

    // ComboBox subclass that ignores mouse-wheel events so trackpad scroll
    // cannot accidentally change a routing selection.
    struct FixedComboBox : public juce::ComboBox
    {
        using juce::ComboBox::ComboBox;
        void mouseWheelMove (const juce::MouseEvent&,
                             const juce::MouseWheelDetails&) override {}
    };

    // MenuBarModel
    juce::StringArray getMenuBarNames() override { return {}; }
    juce::PopupMenu   getMenuForIndex (int, const juce::String&) override { return {}; }
    void              menuItemSelected (int menuItemID, int) override;

    void populateRoutingCombos();
    void populateProjectCombo();
    void checkRoutingSafety (FixedComboBox* changed);
    void lockRouting();    // disable routing controls after first measurement
    void unlockRouting();  // re-enable routing controls when project changes
    void onProjectComboChanged();
    void onInitProjectClicked();
    void onBuildPlanClicked();
    juce::String readModeFromSessionJson (const juce::File& folder) const;
    void onMeasureButtonClicked();
    void onAudioSettingsClicked();
    void onAnalyseClicked();
    void timerCallback() override;

    // Loads a named stimulus into AudioEngine from BinaryData.
    // Returns an error string on failure, empty string on success.
    juce::String loadStimulusByName (const juce::String& name);

    // --- Project setup ---
    juce::Label      projectLabel  { {}, "Project:" };
    juce::ComboBox   projectCombo;
    juce::TextEditor projectNameEditor;
    juce::TextButton initProjectButton { "Create Project" };

    // Persists toggle states and routing between sessions.
    juce::ApplicationProperties appProperties;

    // --- Mode selector ---
    juce::Label modeLabel;
    juce::TextButton saturationModeButton  { "Saturation" };
    juce::TextButton compressorModeButton  { "Compressor" };
    juce::ToggleButton instrumentLevelToggle { "Inst Level (-18 dB)" };

    // --- Capture quality selector ---
    juce::Label      qualityLabel  { {}, "Capture Quality:" };
    juce::TextButton quickButton    { "Quick" };
    juce::TextButton standardButton { "Standard" };
    juce::TextButton hifiButton     { "Hi-Fi" };
    CaptureQuality   captureQuality { CaptureQuality::Standard };

    // --- Routing selector ---
    juce::Label routingLabel;
    juce::Label sendLabel       { {}, "Send Output Pair:" };
    juce::Label returnLabel     { {}, "Return Input Pair:" };
    juce::Label monitorLabel    { {}, "Monitor Output Pair:" };
    FixedComboBox sendCombo;
    FixedComboBox returnCombo;
    FixedComboBox monitorCombo;
    juce::ToggleButton monoModeToggle { "Mono device (single channel)" };

    // --- Measurement control ---
    juce::TextButton measureButton  { "Start Measurement" };
    juce::TextButton analyseButton  { "Analyze & Fit Model" };

    // --- Plan editor ---
    juce::Label      planLabel;
    juce::ComboBox   planPresetCombo;
    juce::TextButton buildPlanButton { "Build Plan" };
    PlanListModel    planListModel;
    juce::ListBox    planList;

    // --- UI scale ---
    float            uiScale { 1.0f };
    int              sepY1   { 0 };   // separator Y positions updated in resized()
    int              sepY2   { 0 };
    int              sepY3   { 0 };
    juce::TextButton scaleBtn50  { "50%"  };
    juce::TextButton scaleBtn75  { "75%"  };
    juce::TextButton scaleBtn100 { "100%" };
    juce::TextButton scaleBtn125 { "125%" };

    // --- Input level meter ---
    LevelMeter returnMeter;

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

    // True once the first measurement has started for the current project.
    // Routing controls are locked until a new/different project is opened.
    bool routingLocked { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
