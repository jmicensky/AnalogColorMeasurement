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
    projectNameEditor.setInputRestrictions (64);   // reasonable max length
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

    // --- Plan editor shell ---
    planLabel.setText ("Stimulus Plan", juce::dontSendNotification);
    planLabel.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
    addAndMakeVisible (planLabel);

    addAndMakeVisible (planList);

    // --- Status bar ---
    auto* device = audioEngine.getDeviceManager().getCurrentAudioDevice();
    juce::String deviceName = device ? device->getName() : "No audio device";
    statusLabel.setText ("Device: " + deviceName + "  |  No project initialized.",
                         juce::dontSendNotification);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    setSize (900, 680);
}

MainComponent::~MainComponent() {}

//==============================================================================
void MainComponent::onInitProjectClicked()
{
    // Read the mode from whichever toggle button is active.
    juce::String mode = saturationModeButton.getToggleState() ? "saturation"
                                                               : "compressor";

    auto err = sessionWriter.initialise (projectNameEditor.getText(), mode);

    if (err.isNotEmpty())
    {
        statusLabel.setText ("ERROR: " + err, juce::dontSendNotification);
        return;
    }

    // Success — update the button and status bar to confirm the project is live.
    initProjectButton.setButtonText ("Project: " + sessionWriter.getProjectName());
    statusLabel.setText ("Project initialized: "
                         + sessionWriter.getProjectFolder().getFullPathName(),
                         juce::dontSendNotification);
}

void MainComponent::onAudioSettingsClicked()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (
        audioEngine.getDeviceManager(),
        0, 32,    // input channels
        0, 32,    // output channels
        false, false, true, false);

    selector->setSize (500, 450);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (selector);
    opts.dialogTitle                = "Audio Device Settings";
    opts.dialogBackgroundColour     = getLookAndFeel().findColour (
                                          juce::ResizableWindow::backgroundColourId);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar          = true;
    opts.resizable                  = true;

    auto* dialog = opts.launchAsync();

    // Re-populate routing combos and refresh status when the dialog is dismissed,
    // since the user may have changed the device or its channel layout.
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
    struct RefEntry { const void* data; size_t size; const char* stem; };

    const RefEntry entries[] = {
        { BinaryData::DRUMS_testtone_wav,    (size_t) BinaryData::DRUMS_testtone_wavSize,    "DRUMS"    },
        { BinaryData::BASSDI_testtone_wav,   (size_t) BinaryData::BASSDI_testtone_wavSize,   "BASSDI"   },
        { BinaryData::ElecKeys_testtone_wav, (size_t) BinaryData::ElecKeys_testtone_wavSize, "ElecKeys" },
    };

    const int id = refFileCombo.getSelectedId();
    if (id < 2 || id > 4)
        return;

    const auto& e = entries[id - 2];
    auto err = audioEngine.loadReferenceFileFromMemory (e.data, e.size, e.stem);

    if (err.isNotEmpty())
        statusLabel.setText ("ERROR loading ref file: " + err, juce::dontSendNotification);
    else
        statusLabel.setText ("Reference file loaded: " + juce::String (e.stem),
                             juce::dontSendNotification);
}

void MainComponent::timerCallback()
{
    if (! audioEngine.isFinished())
        return;

    stopTimer();
    measureButton.setToggleState (false, juce::dontSendNotification);
    measureButton.setButtonText ("Start Measurement");

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

    // Gate 1: project must be initialized before any files can be written.
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

    // Decode channel pair index from combo ID: firstChannel = (id - 2) * 2
    const int sendCh    = (sendCombo   .getSelectedId() - 2) * 2;
    const int monitorCh = (monitorCombo.getSelectedId() == 1)
                              ? -1
                              : (monitorCombo.getSelectedId() - 2) * 2;

    // Gate 3: a reference file must be loaded.
    if (audioEngine.getReferenceFileStem().isEmpty())
    {
        measureButton.setToggleState (false, juce::dontSendNotification);
        statusLabel.setText ("ERROR: Select a Reference File before measuring.",
                             juce::dontSendNotification);
        return;
    }

    audioEngine.setSendChannelPair    (sendCh);
    audioEngine.setMonitorChannelPair (monitorCh);
    audioEngine.startMeasurement();
    startTimer (100);   // poll isFinished() every 100 ms

    measureButton.setButtonText ("Stop Measurement");
    statusLabel.setText ("Measuring...  Project: " + sessionWriter.getProjectName()
                         + "  Mode: " + sessionWriter.getMode(),
                         juce::dontSendNotification);
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
    // Dividers below: ref-file section, project section, mode section, routing section, status bar
    g.drawHorizontalLine (52,               10.0f, (float) getWidth() - 10);
    g.drawHorizontalLine (96,               10.0f, (float) getWidth() - 10);
    g.drawHorizontalLine (152,              10.0f, (float) getWidth() - 10);
    g.drawHorizontalLine (282,              10.0f, (float) getWidth() - 10);
    g.drawHorizontalLine (getHeight() - 30, 10.0f, (float) getWidth() - 10);
}

void MainComponent::resized()
{
    const int margin  = 12;
    const int rowH    = 28;
    const int labelW  = 160;
    int y = margin;

    // --- Reference file selector ---
    refFileLabel.setBounds (margin,          y, labelW, rowH);
    refFileCombo.setBounds (margin + labelW, y, 220,    rowH);
    y += rowH + margin;

    // --- Project setup ---
    projectLabel    .setBounds (margin,                y, 110,            rowH);
    projectNameEditor.setBounds (margin + 115,         y, 260,            rowH);
    initProjectButton.setBounds (margin + 115 + 268,   y, 160,            rowH);
    y += rowH + margin + 4;

    // --- Mode selector ---
    modeLabel.setBounds (margin, y, 200, 20);
    y += 22;
    saturationModeButton.setBounds (margin,        y, 130, rowH);
    compressorModeButton.setBounds (margin + 140,  y, 130, rowH);
    y += rowH + margin + 14;

    // --- Routing selector ---
    routingLabel.setBounds (margin, y, 200, 20);
    audioSettingsButton.setBounds (getWidth() - margin - 150, y, 150, 20);
    y += 24;
    sendLabel   .setBounds (margin,          y, labelW, rowH);
    sendCombo   .setBounds (margin + labelW, y, 220,    rowH);
    y += rowH + 6;
    returnLabel .setBounds (margin,          y, labelW, rowH);
    returnCombo .setBounds (margin + labelW, y, 220,    rowH);
    y += rowH + 6;
    monitorLabel.setBounds (margin,          y, labelW, rowH);
    monitorCombo.setBounds (margin + labelW, y, 220,    rowH);
    y += rowH + margin + 6;

    // --- Measurement control ---
    measureButton.setBounds (margin, y, 200, rowH);
    y += rowH + margin + 8;

    // --- Plan editor shell ---
    planLabel.setBounds (margin, y, 200, 20);
    y += 24;
    planList.setBounds (margin, y, getWidth() - margin * 2, getHeight() - y - 40);

    // --- Status bar ---
    statusLabel.setBounds (margin, getHeight() - 28, getWidth() - margin * 2, 24);
}
