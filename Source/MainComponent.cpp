#include "MainComponent.h"

MainComponent::MainComponent()
{
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

    // Attach safety callbacks. Each lambda captures `this` and passes a pointer
    // to the combo that just changed, so checkRoutingSafety() knows which one
    // to revert if a conflict is detected.
    sendCombo   .onChange = [this] { checkRoutingSafety (&sendCombo); };
    monitorCombo.onChange = [this] { checkRoutingSafety (&monitorCombo); };

    // --- Plan editor shell ---
    planLabel.setText ("Stimulus Plan", juce::dontSendNotification);
    planLabel.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
    addAndMakeVisible (planLabel);

    addAndMakeVisible (planList);

    // --- Status bar ---
    // Query the AudioDeviceManager for the currently open device name.
    // getCurrentAudioDevice() returns nullptr if no device is open yet.
    auto* device = audioEngine.getDeviceManager().getCurrentAudioDevice();
    juce::String deviceName = device ? device->getName() : "No audio device";
    statusLabel.setText ("Device: " + deviceName, juce::dontSendNotification);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    setSize (900, 600);
}

MainComponent::~MainComponent() {}

void MainComponent::populateRoutingCombos()
{
    // Clear all three combos and reset to "(not set)" as item ID 1.
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

    // getOutputChannelNames() / getInputChannelNames() return a StringArray
    // where each element is the hardware label for one channel (e.g. "Line 1").
    // We group them into stereo pairs: channels i and i+1.
    // Item ID = (pairIndex + 2) so ID 1 is reserved for "(not set)".

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

    // ID 1 means "(not set)" — only flag a conflict when both sides have a
    // real hardware pair selected and they resolve to the same output channels.
    // {Send outputs} ∩ {Monitor outputs} must equal ∅  (proposal eq. 2).
    if (sendId != 1 && monitorId != 1 && sendId == monitorId)
    {
        // Revert whichever combo the user just touched back to "(not set)".
        // dontSendNotification prevents this revert from re-triggering onChange.
        changed->setSelectedId (1, juce::dontSendNotification);
        statusLabel.setText ("WARNING: Send and Monitor cannot share the same output pair.",
                             juce::dontSendNotification);
        return;
    }

    // Routing is safe — restore the normal device name in the status bar.
    auto* device = audioEngine.getDeviceManager().getCurrentAudioDevice();
    juce::String deviceName = device ? device->getName() : "No audio device";
    statusLabel.setText ("Device: " + deviceName, juce::dontSendNotification);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    // Section dividers
    g.setColour (juce::Colours::grey);
    g.drawHorizontalLine (50,  10.0f, (float) getWidth() - 10);
    g.drawHorizontalLine (170, 10.0f, (float) getWidth() - 10);
    g.drawHorizontalLine (getHeight() - 30, 10.0f, (float) getWidth() - 10);
}

void MainComponent::resized()
{
    const int margin  = 12;
    const int rowH    = 28;
    const int labelW  = 160;
    int y = margin;

    // --- Mode selector ---
    modeLabel.setBounds (margin, y, 200, 20);
    y += 24;
    saturationModeButton.setBounds (margin,          y, 130, rowH);
    compressorModeButton.setBounds (margin + 140,    y, 130, rowH);
    y += rowH + margin + 14; // extra gap for divider

    // --- Routing selector ---
    routingLabel.setBounds (margin, y, 200, 20);
    y += 24;
    sendLabel  .setBounds (margin,          y, labelW, rowH);
    sendCombo  .setBounds (margin + labelW, y, 220,    rowH);
    y += rowH + 6;
    returnLabel.setBounds (margin,          y, labelW, rowH);
    returnCombo.setBounds (margin + labelW, y, 220,    rowH);
    y += rowH + 6;
    monitorLabel.setBounds (margin,          y, labelW, rowH);
    monitorCombo.setBounds (margin + labelW, y, 220,    rowH);
    y += rowH + margin + 14;

    // --- Plan editor shell ---
    planLabel.setBounds (margin, y, 200, 20);
    y += 24;
    planList.setBounds (margin, y, getWidth() - margin * 2, getHeight() - y - 40);

    // --- Status bar ---
    statusLabel.setBounds (margin, getHeight() - 28, getWidth() - margin * 2, 24);
}
