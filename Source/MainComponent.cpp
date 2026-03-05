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

    sendCombo.addItem ("(not set)", 1);
    sendCombo.setSelectedId (1);
    addAndMakeVisible (sendCombo);

    returnCombo.addItem ("(not set)", 1);
    returnCombo.setSelectedId (1);
    addAndMakeVisible (returnCombo);

    monitorCombo.addItem ("(not set)", 1);
    monitorCombo.setSelectedId (1);
    addAndMakeVisible (monitorCombo);

    // --- Plan editor shell ---
    planLabel.setText ("Stimulus Plan", juce::dontSendNotification);
    planLabel.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
    addAndMakeVisible (planLabel);

    addAndMakeVisible (planList);

    // --- Status bar ---
    statusLabel.setText ("Ready", juce::dontSendNotification);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    setSize (900, 600);
}

MainComponent::~MainComponent() {}

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
