#include "PluginEditor.h"

HardwareColorEditor::HardwareColorEditor (HardwareColorProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    // --- Knobs ---
    for (auto* s : { &driveSlider, &weightSlider, &mixSlider, &outputGainSlider })
        addAndMakeVisible (s);

    for (auto* l : { &driveLabel, &weightLabel, &mixLabel, &outputGainLabel })
    {
        l->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    }

    driveAttach      = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "drive",       driveSlider);
    weightAttach     = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "weight",      weightSlider);
    mixAttach        = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mix",         mixSlider);
    outputGainAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "outputGain",  outputGainSlider);

    // --- Artifact loader ---
    loadButton.addListener (this);
    addAndMakeVisible (loadButton);

    modelLabel.setJustificationType (juce::Justification::centredLeft);
    modelLabel.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
    addAndMakeVisible (modelLabel);

    // --- Spectrum display ---
    addAndMakeVisible (spectrumDisplay);

    updateModelLabel();

    setSize (500, 460);
}

HardwareColorEditor::~HardwareColorEditor()
{
    loadButton.removeListener (this);
}

//==============================================================================
void HardwareColorEditor::buttonClicked (juce::Button* btn)
{
    if (btn != &loadButton) return;

    auto chooser = std::make_shared<juce::FileChooser> (
        "Load Hardware Color Model",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("HardwareProfiler"),
        "*.json");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            if (fc.getResults().isEmpty()) return;

            const juce::String err = processor.loadArtifact (fc.getResult());
            if (err.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "Load failed", err);
            updateModelLabel();
        });
}

void HardwareColorEditor::updateModelLabel()
{
    if (processor.hasModel())
    {
        const auto& m = processor.getModel();
        modelLabel.setText ("Model: " + m.deviceName
                            + "  |  " + juce::String (m.sampleRate / 1000.0, 1) + " kHz"
                            + "  |  " + juce::String ((int) m.thdResults.size()) + " THD pts",
                            juce::dontSendNotification);
        spectrumDisplay.setData (m.frFrequencies, m.frMagnitudeDb);
    }
    else
    {
        modelLabel.setText ("No model loaded.", juce::dontSendNotification);
        spectrumDisplay.setData ({}, {});
    }
}

//==============================================================================
void HardwareColorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e2e));

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions().withHeight (18.0f).withStyle ("Bold")));
    g.drawText ("Hardware Color", getLocalBounds().removeFromTop (36),
                juce::Justification::centred);
}

void HardwareColorEditor::resized()
{
    const int margin  = 12;
    const int knobW   = 100;
    const int knobH   = 100;
    const int labelH  = 20;
    const int btnH    = 28;
    const int totalKnobsW = knobW * 4 + margin * 3;
    const int knobStartX  = (getWidth() - totalKnobsW) / 2;

    // Title row: top 36 px (painted, not a component).
    int y = 44;

    // Knobs row.
    for (auto [slider, label, col] :
         std::initializer_list<std::tuple<juce::Slider*, juce::Label*, int>>
         { { &driveSlider,      &driveLabel,      0 },
           { &weightSlider,     &weightLabel,     1 },
           { &mixSlider,        &mixLabel,        2 },
           { &outputGainSlider, &outputGainLabel, 3 } })
    {
        const int x = knobStartX + col * (knobW + margin);
        slider->setBounds (x, y,          knobW, knobH);
        label ->setBounds (x, y + knobH,  knobW, labelH);
    }
    y += knobH + labelH + margin;

    // Load button + model label.
    loadButton .setBounds (margin, y, 120, btnH);
    modelLabel .setBounds (margin + 128, y, getWidth() - margin * 2 - 128, btnH);
    y += btnH + margin;

    // Spectrum display.
    spectrumDisplay.setBounds (margin, y, getWidth() - margin * 2, getHeight() - y - margin);
}
