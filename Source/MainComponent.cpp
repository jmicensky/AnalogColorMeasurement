#include "MainComponent.h"
#include <BinaryData.h>

MainComponent::MainComponent()
{
    // --- Reference file selector ---
    refFileLabel.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
    addAndMakeVisible (refFileLabel);

    refFileCombo.addItem ("(select file)", 1);
    refFileCombo.addItem ("DRUMS",         2);
    refFileCombo.addItem ("BASSDI",        3);
    refFileCombo.addItem ("ElecKeys",      4);
    refFileCombo.setSelectedId (1);
    refFileCombo.onChange = [this] { onRefFileSelected(); };
    addAndMakeVisible (refFileCombo);

    // --- Project setup ---
    projectLabel.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
    addAndMakeVisible (projectLabel);

    projectNameEditor.setTextToShowWhenEmpty ("Enter project name...",
                                              juce::Colours::grey);
    projectNameEditor.setInputRestrictions (64);
    addAndMakeVisible (projectNameEditor);

    initProjectButton.onClick = [this] { onInitProjectClicked(); };
    addAndMakeVisible (initProjectButton);

    // --- Mode selector ---
    modeLabel.setText ("Measurement Mode", juce::dontSendNotification);
    modeLabel.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
    addAndMakeVisible (modeLabel);

    saturationModeButton.setClickingTogglesState (true);
    saturationModeButton.setToggleState (true, juce::dontSendNotification);
    addAndMakeVisible (saturationModeButton);

    compressorModeButton.setClickingTogglesState (true);
    addAndMakeVisible (compressorModeButton);

    // --- Routing selector ---
    audioSettingsButton.onClick = [this] { onAudioSettingsClicked(); };
    addAndMakeVisible (audioSettingsButton);

    routingLabel.setText ("Audio Routing", juce::dontSendNotification);
    routingLabel.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
    addAndMakeVisible (routingLabel);

    addAndMakeVisible (sendLabel);
    addAndMakeVisible (returnLabel);
    addAndMakeVisible (monitorLabel);

    addAndMakeVisible (sendCombo);
    addAndMakeVisible (returnCombo);
    addAndMakeVisible (monitorCombo);

    populateRoutingCombos();

    sendCombo   .onChange = [this] { checkRoutingSafety (&sendCombo); };
    monitorCombo.onChange = [this] { checkRoutingSafety (&monitorCombo); };

    // --- Measurement control ---
    measureButton.setClickingTogglesState (true);
    measureButton.onClick = [this] { onMeasureButtonClicked(); };
    addAndMakeVisible (measureButton);

    // --- Plan editor ---
    planLabel.setText ("Stimulus Plan", juce::dontSendNotification);
    planLabel.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
    addAndMakeVisible (planLabel);

    gainLabelsLabel.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
    addAndMakeVisible (gainLabelsLabel);

    gainLabelsEditor.setTextToShowWhenEmpty ("-18, -12, -6, 0, +6",
                                             juce::Colours::grey);
    gainLabelsEditor.setInputRestrictions (128);
    addAndMakeVisible (gainLabelsEditor);

    buildPlanButton.onClick = [this] { onBuildPlanClicked(); };
    addAndMakeVisible (buildPlanButton);

    planListModel.plan = &stimulusPlan;
    planList.setModel (&planListModel);
    planList.setRowHeight (22);
    addAndMakeVisible (planList);

    // --- Status bar ---
    auto* device = audioEngine.getDeviceManager().getCurrentAudioDevice();
    juce::String deviceName = device ? device->getName() : "No audio device";
    statusLabel.setText ("Device: " + deviceName + "  |  No project initialized.",
                         juce::dontSendNotification);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    setSize (900, 720);
}

MainComponent::~MainComponent() {}

//==============================================================================
juce::String MainComponent::loadStimulusByName (const juce::String& name)
{
    struct Entry { const void* data; size_t size; const char* stem; };
    const Entry entries[] = {
        { BinaryData::DRUMS_testtone_wav,    (size_t) BinaryData::DRUMS_testtone_wavSize,    "DRUMS"    },
        { BinaryData::BASSDI_testtone_wav,   (size_t) BinaryData::BASSDI_testtone_wavSize,   "BASSDI"   },
        { BinaryData::ElecKeys_testtone_wav, (size_t) BinaryData::ElecKeys_testtone_wavSize, "ElecKeys" },
    };

    for (const auto& e : entries)
        if (name == e.stem)
            return audioEngine.loadReferenceFileFromMemory (e.data, e.size, e.stem);

    return "Unknown stimulus: " + name;
}

//==============================================================================
void MainComponent::onInitProjectClicked()
{
    juce::String mode = saturationModeButton.getToggleState() ? "saturation"
                                                               : "compressor";

    auto err = sessionWriter.initialise (projectNameEditor.getText(), mode);

    if (err.isNotEmpty())
    {
        statusLabel.setText ("ERROR: " + err, juce::dontSendNotification);
        return;
    }

    initProjectButton.setButtonText ("Project: " + sessionWriter.getProjectName());
    statusLabel.setText ("Project initialized: "
                         + sessionWriter.getProjectFolder().getFullPathName(),
                         juce::dontSendNotification);
}

void MainComponent::onBuildPlanClicked()
{
    if (! sessionWriter.isInitialised())
    {
        statusLabel.setText ("ERROR: Initialize a project before building a plan.",
                             juce::dontSendNotification);
        return;
    }

    const juce::String csv = gainLabelsEditor.getText().trim();
    if (csv.isEmpty())
    {
        statusLabel.setText ("ERROR: Enter at least one gain level before building a plan.",
                             juce::dontSendNotification);
        return;
    }

    stimulusPlan.build (csv);
    markers.clear();

    planList.updateContent();
    planList.repaint();

    const int numGains = juce::StringArray::fromTokens (csv, ",", "").size();
    statusLabel.setText ("Plan built: "
                         + juce::String (stimulusPlan.totalSteps())
                         + " steps  ("
                         + juce::String (numGains)
                         + " gain levels \xc3\x97 "       // UTF-8 multiplication sign
                         + juce::String (StimulusNames::all.size())
                         + " stimuli).  Set hardware to \""
                         + stimulusPlan.getCurrentStep()->gainLabel
                         + "\" then press Start Measurement.",
                         juce::dontSendNotification);
}

void MainComponent::onAudioSettingsClicked()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (
        audioEngine.getDeviceManager(),
        0, 32,
        0, 32,
        false, false, true, false);

    selector->setSize (500, 450);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (selector);
    opts.dialogTitle                  = "Audio Device Settings";
    opts.dialogBackgroundColour       = getLookAndFeel().findColour (
                                            juce::ResizableWindow::backgroundColourId);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar            = true;
    opts.resizable                    = true;

    auto* dialog = opts.launchAsync();

    juce::ModalComponentManager::getInstance()->attachCallback (
        dialog,
        juce::ModalCallbackFunction::create ([this] (int)
        {
            populateRoutingCombos();
            auto* device = audioEngine.getDeviceManager().getCurrentAudioDevice();
            juce::String deviceName = device ? device->getName() : "No audio device";
            statusLabel.setText ("Device: " + deviceName, juce::dontSendNotification);
        }));
}

void MainComponent::onRefFileSelected()
{
    const int id = refFileCombo.getSelectedId();
    if (id < 2)
        return;

    const juce::StringArray names { "DRUMS", "BASSDI", "ElecKeys" };
    const juce::String name = names[id - 2];

    auto err = loadStimulusByName (name);
    if (err.isNotEmpty())
        statusLabel.setText ("ERROR loading ref file: " + err, juce::dontSendNotification);
    else
        statusLabel.setText ("Reference file loaded: " + name, juce::dontSendNotification);
}

void MainComponent::timerCallback()
{
    if (! audioEngine.isFinished())
        return;

    stopTimer();
    measureButton.setToggleState (false, juce::dontSendNotification);
    measureButton.setButtonText ("Start Measurement");

    // --- Plan-driven path ---
    if (! stimulusPlan.isEmpty() && ! stimulusPlan.isComplete())
    {
        // Snapshot the current step before advancing.
        const StimulusStep step = *stimulusPlan.getCurrentStep();

        auto refPath = sessionWriter.getStepRefFilePath (step);
        auto recPath = sessionWriter.getStepRecFilePath (step);

        const int lag = LatencyAligner::findLatencySamples (
            audioEngine.getReferenceBuffer(),
            audioEngine.getRecordBuffer(),
            audioEngine.getCapturePosition(),
            audioEngine.getSampleRate());
        audioEngine.trimRecBuffer (lag);

        auto err = audioEngine.writeSession (refPath, recPath);
        if (err.isNotEmpty())
        {
            statusLabel.setText ("ERROR writing session: " + err, juce::dontSendNotification);
            return;
        }

        // Append marker.
        MarkerEntry marker;
        marker.stepIndex    = stimulusPlan.currentStepIndex();
        marker.gainLabel    = step.gainLabel;
        marker.stimulusName = step.stimulusName;
        marker.refFile      = refPath.getFileName();
        marker.recFile      = recPath.getFileName();
        marker.timestamp    = juce::Time::getCurrentTime().toISO8601 (true);
        markers.add (marker);

        // Advance the plan and refresh the list view.
        stimulusPlan.advance();
        planList.updateContent();
        planList.repaint();

        // Write metadata after every completed step so nothing is lost on abort.
        auto* device    = audioEngine.getDeviceManager().getCurrentAudioDevice();
        const float sr  = device ? (float) device->getCurrentSampleRate() : 44100.0f;
        const int sendCh    = (sendCombo   .getSelectedId() - 2) * 2;
        const int returnCh  = (returnCombo .getSelectedId() - 2) * 2;
        const int monitorCh = (monitorCombo.getSelectedId() == 1)
                                  ? -1
                                  : (monitorCombo.getSelectedId() - 2) * 2;

        sessionWriter.writeSessionJson (stimulusPlan.getSteps(), sr, sendCh, returnCh, monitorCh, lag);
        sessionWriter.writeMarkersJson (markers);

        if (stimulusPlan.isComplete())
        {
            statusLabel.setText ("Session complete!  All "
                                 + juce::String (stimulusPlan.totalSteps())
                                 + " steps saved to: "
                                 + sessionWriter.getProjectFolder().getFullPathName(),
                                 juce::dontSendNotification);
        }
        else
        {
            const auto* next = stimulusPlan.getCurrentStep();
            statusLabel.setText ("Step "
                                 + juce::String (stimulusPlan.currentStepIndex())
                                 + " / "
                                 + juce::String (stimulusPlan.totalSteps())
                                 + " saved.  Next: \""
                                 + next->gainLabel + "\"  \xe2\x80\x94  " + next->stimulusName,
                                 juce::dontSendNotification);
        }

        return;
    }

    // --- Legacy single-capture path (no plan active) ---
    const int lag = LatencyAligner::findLatencySamples (
        audioEngine.getReferenceBuffer(),
        audioEngine.getRecordBuffer(),
        audioEngine.getCapturePosition(),
        audioEngine.getSampleRate());
    audioEngine.trimRecBuffer (lag);

    auto err = audioEngine.writeSession (sessionWriter.getRefFilePath(),
                                         sessionWriter.getRecFilePath());
    if (err.isNotEmpty())
        statusLabel.setText ("ERROR writing session: " + err, juce::dontSendNotification);
    else
        statusLabel.setText ("Session saved to: "
                             + sessionWriter.getProjectFolder().getFullPathName(),
                             juce::dontSendNotification);
}

void MainComponent::onMeasureButtonClicked()
{
    if (audioEngine.isMeasuring())
    {
        stopTimer();
        audioEngine.stopMeasurement();
        measureButton.setButtonText ("Start Measurement");
        statusLabel.setText ("Measurement stopped.", juce::dontSendNotification);
        return;
    }

    // Gate 1: project must be initialized.
    if (! sessionWriter.isInitialised())
    {
        measureButton.setToggleState (false, juce::dontSendNotification);
        statusLabel.setText ("ERROR: Initialize a project before measuring.",
                             juce::dontSendNotification);
        return;
    }

    // Gate 2: Send and Return must be configured.
    if (sendCombo.getSelectedId() == 1)
    {
        measureButton.setToggleState (false, juce::dontSendNotification);
        statusLabel.setText ("ERROR: Select a Send Output Pair before measuring.",
                             juce::dontSendNotification);
        return;
    }

    if (returnCombo.getSelectedId() == 1)
    {
        measureButton.setToggleState (false, juce::dontSendNotification);
        statusLabel.setText ("ERROR: Select a Return Input Pair before measuring.",
                             juce::dontSendNotification);
        return;
    }

    // Gate 3: if a plan is active, auto-load the current step's stimulus.
    //         Otherwise a ref file must already be loaded via the combo.
    if (! stimulusPlan.isEmpty() && ! stimulusPlan.isComplete())
    {
        const auto* step = stimulusPlan.getCurrentStep();
        auto err = loadStimulusByName (step->stimulusName);
        if (err.isNotEmpty())
        {
            measureButton.setToggleState (false, juce::dontSendNotification);
            statusLabel.setText ("ERROR loading stimulus: " + err, juce::dontSendNotification);
            return;
        }
    }
    else if (audioEngine.getReferenceFileStem().isEmpty())
    {
        measureButton.setToggleState (false, juce::dontSendNotification);
        statusLabel.setText ("ERROR: Select a Reference File or build a plan before measuring.",
                             juce::dontSendNotification);
        return;
    }

    // Decode channel pair indices from combo IDs: firstChannel = (id - 2) * 2
    const int sendCh    = (sendCombo   .getSelectedId() - 2) * 2;
    const int returnCh  = (returnCombo .getSelectedId() - 2) * 2;
    const int monitorCh = (monitorCombo.getSelectedId() == 1)
                              ? -1
                              : (monitorCombo.getSelectedId() - 2) * 2;

    audioEngine.setSendChannelPair    (sendCh);
    audioEngine.setReturnChannelPair  (returnCh);    // was missing in Week 2 — fixed
    audioEngine.setMonitorChannelPair (monitorCh);
    audioEngine.startMeasurement();
    startTimer (100);

    measureButton.setButtonText ("Stop Measurement");

    if (! stimulusPlan.isEmpty() && ! stimulusPlan.isComplete())
    {
        const auto* step = stimulusPlan.getCurrentStep();
        statusLabel.setText ("Measuring  ["
                             + juce::String (stimulusPlan.currentStepIndex() + 1)
                             + " / "
                             + juce::String (stimulusPlan.totalSteps())
                             + "]  gain: \""
                             + step->gainLabel + "\"  stimulus: " + step->stimulusName,
                             juce::dontSendNotification);
    }
    else
    {
        statusLabel.setText ("Measuring...  Project: " + sessionWriter.getProjectName()
                             + "  Mode: " + sessionWriter.getMode(),
                             juce::dontSendNotification);
    }
}

//==============================================================================
void MainComponent::populateRoutingCombos()
{
    sendCombo   .clear (juce::dontSendNotification);
    returnCombo .clear (juce::dontSendNotification);
    monitorCombo.clear (juce::dontSendNotification);

    sendCombo   .addItem ("(not set)", 1);
    returnCombo .addItem ("(not set)", 1);
    monitorCombo.addItem ("(not set)", 1);

    auto* device = audioEngine.getDeviceManager().getCurrentAudioDevice();

    if (device == nullptr)
    {
        sendCombo   .setSelectedId (1);
        returnCombo .setSelectedId (1);
        monitorCombo.setSelectedId (1);
        return;
    }

    auto outNames = device->getOutputChannelNames();
    for (int i = 0; i + 1 < outNames.size(); i += 2)
    {
        juce::String label = outNames[i] + " / " + outNames[i + 1];
        int id = (i / 2) + 2;
        sendCombo   .addItem (label, id);
        monitorCombo.addItem (label, id);
    }

    auto inNames = device->getInputChannelNames();
    for (int i = 0; i + 1 < inNames.size(); i += 2)
    {
        juce::String label = inNames[i] + " / " + inNames[i + 1];
        int id = (i / 2) + 2;
        returnCombo.addItem (label, id);
    }

    sendCombo   .setSelectedId (1);
    returnCombo .setSelectedId (1);
    monitorCombo.setSelectedId (1);
}

void MainComponent::checkRoutingSafety (juce::ComboBox* changed)
{
    const int sendId    = sendCombo   .getSelectedId();
    const int monitorId = monitorCombo.getSelectedId();

    if (sendId != 1 && monitorId != 1 && sendId == monitorId)
    {
        changed->setSelectedId (1, juce::dontSendNotification);
        statusLabel.setText ("WARNING: Send and Monitor cannot share the same output pair.",
                             juce::dontSendNotification);
        return;
    }

    auto* device = audioEngine.getDeviceManager().getCurrentAudioDevice();
    juce::String deviceName = device ? device->getName() : "No audio device";
    statusLabel.setText ("Device: " + deviceName, juce::dontSendNotification);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::grey);
    g.drawHorizontalLine (52,               10.0f, (float) getWidth() - 10);
    g.drawHorizontalLine (96,               10.0f, (float) getWidth() - 10);
    g.drawHorizontalLine (152,              10.0f, (float) getWidth() - 10);
    g.drawHorizontalLine (282,              10.0f, (float) getWidth() - 10);
    g.drawHorizontalLine (getHeight() - 30, 10.0f, (float) getWidth() - 10);
}

void MainComponent::resized()
{
    const int margin = 12;
    const int rowH   = 28;
    const int labelW = 160;
    int y = margin;

    // --- Reference file selector ---
    refFileLabel.setBounds (margin,          y, labelW, rowH);
    refFileCombo.setBounds (margin + labelW, y, 220,    rowH);
    y += rowH + margin;

    // --- Project setup ---
    projectLabel     .setBounds (margin,              y, 110, rowH);
    projectNameEditor.setBounds (margin + 115,        y, 260, rowH);
    initProjectButton.setBounds (margin + 115 + 268,  y, 160, rowH);
    y += rowH + margin + 4;

    // --- Mode selector ---
    modeLabel.setBounds (margin, y, 200, 20);
    y += 22;
    saturationModeButton.setBounds (margin,       y, 130, rowH);
    compressorModeButton.setBounds (margin + 140, y, 130, rowH);
    y += rowH + margin + 14;

    // --- Routing selector ---
    routingLabel       .setBounds (margin,                    y, 200, 20);
    audioSettingsButton.setBounds (getWidth() - margin - 150, y, 150, 20);
    y += 24;
    sendLabel  .setBounds (margin,          y, labelW, rowH);
    sendCombo  .setBounds (margin + labelW, y, 220,    rowH);
    y += rowH + 6;
    returnLabel.setBounds (margin,          y, labelW, rowH);
    returnCombo.setBounds (margin + labelW, y, 220,    rowH);
    y += rowH + 6;
    monitorLabel.setBounds (margin,          y, labelW, rowH);
    monitorCombo.setBounds (margin + labelW, y, 220,    rowH);
    y += rowH + margin + 6;

    // --- Measurement control ---
    measureButton.setBounds (margin, y, 200, rowH);
    y += rowH + margin + 8;

    // --- Plan editor ---
    planLabel.setBounds (margin, y, 200, 20);
    y += 24;
    gainLabelsLabel .setBounds (margin,                y, labelW, rowH);
    gainLabelsEditor.setBounds (margin + labelW,       y, 260,    rowH);
    buildPlanButton .setBounds (margin + labelW + 268, y, 110,    rowH);
    y += rowH + 8;
    planList.setBounds (margin, y, getWidth() - margin * 2, getHeight() - y - 40);

    // --- Status bar ---
    statusLabel.setBounds (margin, getHeight() - 28, getWidth() - margin * 2, 24);
}
