// Plugin editor UI providing Drive, Weight, Mix, and Output knobs alongside a frequency-response spectrum display and model-load button.
// The spectrum curve updates in real time as the Weight knob moves, showing the combined hardware FR plus the active tilt EQ shape.

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
    spectrumDisplay.setShowSetupHints (false);
    addAndMakeVisible (spectrumDisplay);

    updateModelLabel();
    startTimerHz (10);   // poll for model changes — ensures repaint in all hosts

    setSize (500, 520);
}

HardwareColorEditor::~HardwareColorEditor()
{
    stopTimer();
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

void HardwareColorEditor::timerCallback()
{
    const bool hasModel = processor.hasModel();
    if (hasModel != lastModelState)
    {
        lastModelState = hasModel;
        updateModelLabel();   // also redraws spectrum
    }

    if (hasModel)
    {
        const float weight = processor.apvts.getRawParameterValue ("weight")->load();
        if (weight != lastWeight)
        {
            lastWeight = weight;
            updateSpectrumDisplay();   // recalculate + repaint on knob movement
        }
        else
        {
            spectrumDisplay.repaint(); // keep display alive in all hosts
        }
    }
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
        updateSpectrumDisplay();
    }
    else
    {
        modelLabel.setText ("No model loaded.", juce::dontSendNotification);
        spectrumDisplay.setData ({}, {});
    }
}

//==============================================================================
// Recomputes the display curve as:  hardware FR  +  weight tilt shelf (in dB)
// This lets the user see the tonal shape at the current Weight knob position.
void HardwareColorEditor::updateSpectrumDisplay()
{
    if (! processor.hasModel()) { spectrumDisplay.setData ({}, {}); return; }

    const auto& m = processor.getModel();
    if (m.frFrequencies.empty()) { spectrumDisplay.setData ({}, {}); return; }

    const float  weight = processor.apvts.getRawParameterValue ("weight")->load();
    const double sr     = processor.getSampleRate() > 0.0
                              ? processor.getSampleRate() : m.sampleRate;

    const int numBins = (int) m.frFrequencies.size();
    std::vector<float> adjustedDb (numBins);

    if (weight > 0.5f + 1e-4f)
    {
        // --- Right of centre: one-pole high-shelf boost at 800 Hz ---
        const float alpha     = std::exp (-juce::MathConstants<float>::twoPi
                                          * 800.0f / (float) sr);
        const float shelfGain = std::pow (10.0f, (weight - 0.5f) * 0.6f);

        for (int i = 0; i < numBins; ++i)
        {
            const float omega = juce::MathConstants<float>::twoPi
                                * m.frFrequencies[i] / (float) sr;
            const float cosOm = std::cos (omega),  sinOm = std::sin (omega);
            const float dRe   = 1.0f - alpha * cosOm,  dIm = alpha * sinOm;
            const float dSq   = dRe*dRe + dIm*dIm;
            const float lpRe  = (1.0f - alpha) * dRe / dSq;
            const float lpIm  = -(1.0f - alpha) * dIm / dSq;
            const float shRe  = shelfGain + (1.0f - shelfGain) * lpRe;
            const float shIm  = (1.0f - shelfGain) * lpIm;
            const float shDb  = 20.0f * std::log10 (
                                    std::max (std::sqrt (shRe*shRe + shIm*shIm), 1e-6f));
            adjustedDb[i] = m.frMagnitudeDb[i] + shDb;
        }
    }
    else if (weight < 0.5f - 1e-4f)
    {
        // --- Left of centre: biquad low-shelf boost at 80 Hz (Pultec-style) ---
        const float gainDb = (0.5f - weight) * 12.0f;
        const auto [b0, b1, b2, a1, a2] =
            HardwareColorProcessor::makeLowShelfCoeffs (gainDb, sr);

        for (int i = 0; i < numBins; ++i)
        {
            const float omega  = juce::MathConstants<float>::twoPi
                                 * m.frFrequencies[i] / (float) sr;
            const float cosW   = std::cos (omega),  sinW = std::sin (omega);
            const float cos2W  = 2.0f*cosW*cosW - 1.0f;
            const float sin2W  = 2.0f*sinW*cosW;
            const float numRe  = b0 + b1*cosW  + b2*cos2W;
            const float numIm  = -(b1*sinW + b2*sin2W);
            const float denRe  = 1.0f + a1*cosW  + a2*cos2W;
            const float denIm  = -(a1*sinW + a2*sin2W);
            const float mag    = std::sqrt (numRe*numRe + numIm*numIm)
                                 / std::max (std::sqrt (denRe*denRe + denIm*denIm), 1e-6f);
            adjustedDb[i] = m.frMagnitudeDb[i] + 20.0f * std::log10 (std::max (mag, 1e-6f));
        }
    }
    else
    {
        // Centre: display raw hardware model response.
        adjustedDb = m.frMagnitudeDb;
    }

    spectrumDisplay.setData (m.frFrequencies, adjustedDb);
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

    // Spectrum display — full plugin width, between title and knobs.
    // Height fills the space not taken by knobs + load row at the bottom.
    const int bottomRowH = knobH + labelH + margin + btnH + margin;
    const int specH      = getHeight() - y - margin - bottomRowH;
    spectrumDisplay.setBounds (margin, y, getWidth() - margin * 2, specH);
    y += specH + margin;

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
    loadButton.setBounds (margin,       y, 120, btnH);
    modelLabel.setBounds (margin + 128, y, getWidth() - margin * 2 - 128, btnH);
}
