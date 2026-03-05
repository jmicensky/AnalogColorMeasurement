#include "MainComponent.h"

MainComponent::MainComponent()
{
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

void MainComponent::onMeasureButtonClicked()
{
    if (audioEngine.isMeasuring())
    {
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

    audioEngine.setSendChannelPair    (sendCh);
    audioEngine.setMonitorChannelPair (monitorCh);
    audioEngine.startMeasurement();

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
    // Dividers below: project section, mode section, routing section, status bar
    g.drawHorizontalLine (44,              10.0f, (float) getWidth() - 10);
    g.drawHorizontalLine (100,             10.0f, (float) getWidth() - 10);
    g.drawHorizontalLine (230,             10.0f, (float) getWidth() - 10);
    g.drawHorizontalLine (getHeight() - 30, 10.0f, (float) getWidth() - 10);
}

void MainComponent::resized()
{
    const int margin  = 12;
    const int rowH    = 28;
    const int labelW  = 160;
    int y = margin;

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
