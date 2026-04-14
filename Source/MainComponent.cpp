#include "MainComponent.h"
#include "SignalGenerator.h"
#include "AnalysisEngine.h"
#include "ArtifactFile.h"
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
    refFileCombo.addItem ("Sine Sweep",    5);
    refFileCombo.addItem ("Pink Noise",    6);
    refFileCombo.addItem ("Sine 1 kHz",    7);
    refFileCombo.addItem ("Sine 100 Hz",   8);
    refFileCombo.setSelectedId (1);
    refFileCombo.onChange = [this] { onRefFileSelected(); };
    addAndMakeVisible (refFileCombo);

    // --- Project setup ---
    projectLabel.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
    addAndMakeVisible (projectLabel);

    populateProjectCombo();
    projectCombo.onChange = [this] { onProjectComboChanged(); };
    addAndMakeVisible (projectCombo);
    onProjectComboChanged();   // auto-initialise whichever project the combo shows on launch

    projectNameEditor.setTextToShowWhenEmpty ("Enter new project name...",
                                              juce::Colours::grey);
    projectNameEditor.setInputRestrictions (64);
    projectNameEditor.setVisible (true);   // visible when "New Project..." is active
    addAndMakeVisible (projectNameEditor);

    initProjectButton.onClick = [this] { onInitProjectClicked(); };
    initProjectButton.setVisible (true);
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

    // --- Capture quality selector ---
    qualityLabel.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
    addAndMakeVisible (qualityLabel);

    auto setupQualityButton = [this] (juce::TextButton& btn, CaptureQuality q)
    {
        btn.setClickingTogglesState (true);
        btn.setToggleState (q == captureQuality, juce::dontSendNotification);
        btn.onClick = [this, &btn, q]
        {
            captureQuality = q;
            quickButton   .setToggleState (&btn == &quickButton,    juce::dontSendNotification);
            standardButton.setToggleState (&btn == &standardButton, juce::dontSendNotification);
            hifiButton    .setToggleState (&btn == &hifiButton,     juce::dontSendNotification);
        };
        addAndMakeVisible (btn);
    };

    setupQualityButton (quickButton,    CaptureQuality::Quick);
    setupQualityButton (standardButton, CaptureQuality::Standard);
    setupQualityButton (hifiButton,     CaptureQuality::HiFi);

    // --- Input level meter ---
    inputMeterLabel.setText ("IN: ---", juce::dontSendNotification);
    inputMeterLabel.setJustificationType (juce::Justification::centred);
    inputMeterLabel.setColour (juce::Label::backgroundColourId, juce::Colours::black);
    inputMeterLabel.setColour (juce::Label::textColourId, juce::Colours::limegreen);
    addAndMakeVisible (inputMeterLabel);

    meterTimer.owner = this;
    meterTimer.startTimer (100);

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

    monoModeToggle.setToggleState (false, juce::dontSendNotification);
    monoModeToggle.onStateChange = [this]
    {
        const bool mono = monoModeToggle.getToggleState();
        audioEngine.setMonoMode (mono);
        // In mono mode the monitor pair is unused — disable it.
        monitorCombo.setEnabled (! mono);
        monitorLabel.setEnabled (! mono);
    };
    addAndMakeVisible (monoModeToggle);

    // --- Measurement control ---
    measureButton.setClickingTogglesState (true);
    measureButton.onClick = [this] { onMeasureButtonClicked(); };
    addAndMakeVisible (measureButton);

    analyseButton.onClick = [this] { onAnalyseClicked(); };
    addAndMakeVisible (analyseButton);

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

    // --- Spectrum display ---
    addAndMakeVisible (spectrumDisplay);

    // --- Status bar ---
    statusLabel.setText (audioEngine.getDeviceStatusString() + "  |  No project initialized.",
                         juce::dontSendNotification);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    setSize (900, 720);
}

MainComponent::~MainComponent() {}

//==============================================================================
juce::String MainComponent::loadStimulusByName (const juce::String& name)
{
    // --- Embedded binary reference files ---
    struct Entry { const void* data; size_t size; const char* stem; };
    const Entry entries[] = {
        { BinaryData::DRUMS_testtone_wav,    (size_t) BinaryData::DRUMS_testtone_wavSize,    "DRUMS"    },
        { BinaryData::BASSDI_testtone_wav,   (size_t) BinaryData::BASSDI_testtone_wavSize,   "BASSDI"   },
        { BinaryData::ElecKeys_testtone_wav, (size_t) BinaryData::ElecKeys_testtone_wavSize, "ElecKeys" },
    };

    for (const auto& e : entries)
        if (name == e.stem)
            return audioEngine.loadReferenceFileFromMemory (e.data, e.size, e.stem);

    // --- Generated test signals ---
    auto* device = audioEngine.getDeviceManager().getCurrentAudioDevice();
    const double sr = device ? device->getCurrentSampleRate() : 44100.0;

    juce::MemoryBlock wav;

    if (name == "SineSweep")
        wav = SignalGenerator::makeLogSineSweep (10.0f, sr);
    else if (name == "PinkNoise")
        wav = SignalGenerator::makePinkNoise    (10.0f, sr);
    else if (name == "Sine1kHz")
        wav = SignalGenerator::makeSineTone     (1000.0f,  10.0f, sr);
    else if (name == "Sine100Hz")
        wav = SignalGenerator::makeSineTone     (100.0f, 10.0f, sr);
    else
        return "Unknown stimulus: " + name;

    return audioEngine.loadReferenceFileFromMemory (wav.getData(), wav.getSize(), name);
}

//==============================================================================
void MainComponent::MeterTimer::timerCallback()
{
    const float db = owner->audioEngine.getReturnPeakDb();
    juce::String text;
    if (db <= -100.0f)
        text = "IN: silence";
    else
        text = "IN: " + juce::String (db, 1) + " dBFS";

    // Colour: green below -18, yellow -18 to -6, red above -6
    juce::Colour colour = (db < -18.0f) ? juce::Colours::limegreen
                        : (db < -6.0f)  ? juce::Colours::yellow
                                         : juce::Colours::red;

    owner->inputMeterLabel.setText (text, juce::dontSendNotification);
    owner->inputMeterLabel.setColour (juce::Label::textColourId, colour);
}

void MainComponent::populateProjectCombo()
{
    const int prevId = projectCombo.getSelectedId();

    projectCombo.clear (juce::dontSendNotification);
    projectCombo.addItem ("+ New Project...", 1);

    const juce::File baseDir = juce::File::getSpecialLocation (
        juce::File::userDocumentsDirectory).getChildFile ("HardwareProfiler");

    if (baseDir.isDirectory())
    {
        // Collect subdirs that are not raw-archive folders, sorted newest first.
        juce::Array<juce::File> folders;
        for (const auto& f : baseDir.findChildFiles (juce::File::findDirectories, false))
            if (! f.getFileName().endsWith ("_raw"))
                folders.add (f);

        std::sort (folders.begin(), folders.end(), [] (const juce::File& a, const juce::File& b) {
            return a.getLastModificationTime() > b.getLastModificationTime();
        });

        for (int i = 0; i < folders.size(); ++i)
            projectCombo.addItem (folders[i].getFileName(), i + 2);
    }

    // Restore previous selection if it still exists, otherwise default to item 1.
    if (prevId > 0 && projectCombo.indexOfItemId (prevId) >= 0)
        projectCombo.setSelectedId (prevId, juce::dontSendNotification);
    else
        projectCombo.setSelectedId (1, juce::dontSendNotification);

    // Show new-project editor only when item 1 ("+ New Project...") is active.
    const bool isNew = (projectCombo.getSelectedId() == 1);
    projectNameEditor.setVisible (isNew);
    initProjectButton.setVisible (isNew);
}

void MainComponent::onProjectComboChanged()
{
    const int id = projectCombo.getSelectedId();
    const bool isNew = (id == 1);

    projectNameEditor.setVisible (isNew);
    initProjectButton.setVisible (isNew);
    resized();   // reflow layout to reclaim / restore the editor row

    if (isNew)
        return;

    // Existing project selected — auto-initialise.
    const juce::String name = projectCombo.getItemText (projectCombo.indexOfItemId (id));
    const juce::String mode = readModeFromSessionJson (
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("HardwareProfiler")
            .getChildFile (name));

    // Sync mode buttons to whatever was recorded in the session file.
    saturationModeButton.setToggleState (mode == "saturation", juce::dontSendNotification);
    compressorModeButton.setToggleState (mode == "compressor", juce::dontSendNotification);

    auto err = sessionWriter.initialise (name, mode);
    if (err.isNotEmpty())
    {
        statusLabel.setText ("ERROR opening project: " + err, juce::dontSendNotification);
        return;
    }

    statusLabel.setText ("Opened project: "
                         + sessionWriter.getProjectFolder().getFullPathName(),
                         juce::dontSendNotification);
}

juce::String MainComponent::readModeFromSessionJson (const juce::File& folder) const
{
    const auto json = juce::JSON::parse (
        folder.getChildFile ("session.json").loadFileAsString());

    if (const auto* obj = json.getDynamicObject())
        if (obj->hasProperty ("mode"))
            return obj->getProperty ("mode").toString();

    return "saturation";   // sensible default if session.json absent
}

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

    populateProjectCombo();

    // Select the newly created project in the combo (it will be the newest folder).
    for (int i = 0; i < projectCombo.getNumItems(); ++i)
    {
        if (projectCombo.getItemText (i) == sessionWriter.getProjectName())
        {
            projectCombo.setSelectedId (projectCombo.getItemId (i), juce::dontSendNotification);
            break;
        }
    }

    // Hide the new-project editor row now that a project is active.
    projectNameEditor.setVisible (false);
    initProjectButton.setVisible (false);
    resized();

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

    stimulusPlan.build (csv, captureQuality);
    markers.clear();

    // Resume: advance past any steps whose ref file already exists on disk.
    int resumedSteps = 0;
    while (! stimulusPlan.isComplete())
    {
        const auto* step = stimulusPlan.getCurrentStep();
        if (sessionWriter.getStepRefFilePath (*step).existsAsFile())
        {
            stimulusPlan.advance();
            ++resumedSteps;
        }
        else
        {
            break;
        }
    }

    planList.updateContent();
    planList.repaint();

    const int numGains   = juce::StringArray::fromTokens (csv, ",", "").size();
    const int numStimuli = (int) StimulusNames::forQuality (captureQuality).size();
    const juce::String qualityName = (captureQuality == CaptureQuality::Quick)    ? "Quick"
                                   : (captureQuality == CaptureQuality::Standard) ? "Standard"
                                                                                   : "Hi-Fi";
    juce::String statusMsg = "Plan built: "
                         + juce::String (stimulusPlan.totalSteps())
                         + " steps  ("
                         + juce::String (numGains)
                         + " gain levels \xc3\x97 "
                         + juce::String (numStimuli)
                         + " stimuli, "
                         + qualityName + ").";

    if (resumedSteps > 0)
        statusMsg += "  Resumed: " + juce::String (resumedSteps) + " steps already done.";

    if (! stimulusPlan.isComplete())
        statusMsg += "  Set hardware to \""
                     + stimulusPlan.getCurrentStep()->gainLabel
                     + "\" then press Start Measurement.";
    else
        statusMsg += "  All steps already complete — ready to Analyze.";

    statusLabel.setText (statusMsg, juce::dontSendNotification);
}

void MainComponent::onAnalyseClicked()
{
    if (! sessionWriter.isInitialised())
    {
        statusLabel.setText ("ERROR: Initialize a project before analyzing.",
                             juce::dontSendNotification);
        return;
    }

    statusLabel.setText ("Analyzing captures...", juce::dontSendNotification);

    LNLModel model;
    const juce::String err = AnalysisEngine::analyseProjectFolder (
        sessionWriter.getProjectFolder(),
        sessionWriter.getProjectName(),
        model);

    if (err.isNotEmpty())
    {
        statusLabel.setText ("ERROR analyzing: " + err, juce::dontSendNotification);
        return;
    }

    // model.sampleRate is set by AnalysisEngine from the WAV file headers —
    // do not overwrite it with the device's current rate here, which may differ
    // if the user changed the device between capture and analysis.

    const juce::File artifactFile = sessionWriter.getProjectFolder()
        .getChildFile (sessionWriter.getProjectName() + "_model.json");

    const juce::String saveErr = ArtifactFile::save (model, artifactFile);
    if (saveErr.isNotEmpty())
    {
        statusLabel.setText ("ERROR saving artifact: " + saveErr, juce::dontSendNotification);
        return;
    }

    spectrumDisplay.setData (model.frFrequencies, model.frMagnitudeDb);

    juce::String summary = "Model saved: " + artifactFile.getFileName()
        + "  |  THD entries: " + juce::String ((int) model.thdResults.size())
        + "  |  FR bins: "     + juce::String ((int) model.frFrequencies.size())
        + "  |  Waveshaper: "  + juce::String ((int) model.waveshaper.size()) + " pts";
    statusLabel.setText (summary, juce::dontSendNotification);
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
            audioEngine.saveDeviceSettings();
            populateRoutingCombos();
            statusLabel.setText (audioEngine.getDeviceStatusString(),
                                 juce::dontSendNotification);
        }));
}

void MainComponent::onRefFileSelected()
{
    const int id = refFileCombo.getSelectedId();
    if (id < 2)
        return;

    const juce::StringArray names { "DRUMS", "BASSDI", "ElecKeys",
                                    "SineSweep", "PinkNoise", "Sine1kHz", "Sine100Hz" };
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

        const float lag = LatencyAligner::findLatencySamples (
            audioEngine.getReferenceBuffer(),
            audioEngine.getRecordBuffer(),
            audioEngine.getCapturePosition(),
            audioEngine.getSampleRate());

        // Ensure per-gain subfolders exist in both working and archive locations.
        refPath.getParentDirectory().createDirectory();
        sessionWriter.getStepArchiveRefFilePath (step).getParentDirectory().createDirectory();

        // Write raw (pre-trim) copies to archive folder BEFORE any processing.
        audioEngine.writeSession (sessionWriter.getStepArchiveRefFilePath (step),
                                  sessionWriter.getStepArchiveRecFilePath (step));

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
        const bool monoTimer = monoModeToggle.getToggleState();
        const int sendCh    = monoTimer ? (sendCombo.getSelectedId() - 2)
                                        : (sendCombo.getSelectedId() - 2) * 2;
        const int returnCh  = monoTimer ? (returnCombo.getSelectedId() - 2)
                                        : (returnCombo.getSelectedId() - 2) * 2;
        const int monitorCh = (monoTimer || monitorCombo.getSelectedId() == 1)
                                  ? -1
                                  : (monitorCombo.getSelectedId() - 2) * 2;

        sessionWriter.writeSessionJson (stimulusPlan.getSteps(), sr, sendCh, returnCh, monitorCh, (int) lag);
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
    const float lag = LatencyAligner::findLatencySamples (
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

    // Decode channel indices from combo IDs.
    // Stereo: firstChannel = (id - 2) * 2.  Mono: channel = id - 2.
    const bool monoNow = monoModeToggle.getToggleState();
    const int sendCh    = monoNow ? (sendCombo.getSelectedId() - 2)
                                  : (sendCombo.getSelectedId() - 2) * 2;
    const int returnCh  = monoNow ? (returnCombo.getSelectedId() - 2)
                                  : (returnCombo.getSelectedId() - 2) * 2;
    const int monitorCh = (monoNow || monitorCombo.getSelectedId() == 1)
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
    const bool mono = monoModeToggle.getToggleState();

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
    if (mono)
    {
        // Mono: list every single output channel.
        for (int i = 0; i < outNames.size(); ++i)
            sendCombo.addItem (outNames[i], i + 2);
    }
    else
    {
        for (int i = 0; i + 1 < outNames.size(); i += 2)
        {
            juce::String label = outNames[i] + " / " + outNames[i + 1];
            int id = (i / 2) + 2;
            sendCombo   .addItem (label, id);
            monitorCombo.addItem (label, id);
        }
    }

    auto inNames = device->getInputChannelNames();
    if (mono)
    {
        for (int i = 0; i < inNames.size(); ++i)
            returnCombo.addItem (inNames[i], i + 2);
    }
    else
    {
        for (int i = 0; i + 1 < inNames.size(); i += 2)
        {
            juce::String label = inNames[i] + " / " + inNames[i + 1];
            int id = (i / 2) + 2;
            returnCombo.addItem (label, id);
        }
    }

    sendCombo   .setSelectedId (1);
    returnCombo .setSelectedId (1);
    monitorCombo.setSelectedId (1);

    monitorCombo.setEnabled (! mono);
    monitorLabel.setEnabled (! mono);
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
    g.drawHorizontalLine (52,               10.0f, (float) getWidth() - 10);  // below ref selector
    g.drawHorizontalLine (96,               10.0f, (float) getWidth() - 10);  // below project
    g.drawHorizontalLine (152,              10.0f, (float) getWidth() - 10);  // below mode
    g.drawHorizontalLine (318,              10.0f, (float) getWidth() - 10);  // below routing+mono
    g.drawHorizontalLine (getHeight() - 30, 10.0f, (float) getWidth() - 10);  // above status
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
    projectLabel.setBounds (margin,        y, 80,  rowH);
    projectCombo.setBounds (margin + 85,   y, 340, rowH);
    y += rowH + 6;

    if (projectNameEditor.isVisible())
    {
        projectNameEditor.setBounds (margin + 85,       y, 240, rowH);
        initProjectButton.setBounds (margin + 85 + 248, y, 130, rowH);
        y += rowH + 6;
    }
    y += 4;

    // --- Mode selector ---
    modeLabel.setBounds (margin, y, 200, 20);
    y += 22;
    saturationModeButton.setBounds (margin,       y, 130, rowH);
    compressorModeButton.setBounds (margin + 140, y, 130, rowH);
    y += rowH + 8;

    // --- Capture quality selector ---
    qualityLabel  .setBounds (margin,       y, 130, rowH);
    quickButton   .setBounds (margin + 135, y,  80, rowH);
    standardButton.setBounds (margin + 225, y,  90, rowH);
    hifiButton    .setBounds (margin + 325, y,  70, rowH);
    y += rowH + margin + 6;

    // --- Routing selector ---
    routingLabel       .setBounds (margin,                    y, 200, 20);
    audioSettingsButton.setBounds (getWidth() - margin - 150, y, 150, 20);
    y += 24;
    sendLabel  .setBounds (margin,          y, labelW, rowH);
    sendCombo  .setBounds (margin + labelW, y, 220,    rowH);
    y += rowH + 6;
    returnLabel     .setBounds (margin,              y, labelW, rowH);
    returnCombo     .setBounds (margin + labelW,     y, 220,    rowH);
    inputMeterLabel .setBounds (margin + labelW + 228, y, 130,  rowH);
    y += rowH + 6;
    monitorLabel.setBounds (margin,          y, labelW, rowH);
    monitorCombo.setBounds (margin + labelW, y, 220,    rowH);
    y += rowH + 6;
    monoModeToggle.setBounds (margin, y, 260, rowH);
    y += rowH + margin + 2;

    // --- Measurement control ---
    measureButton.setBounds (margin,       y, 200, rowH);
    analyseButton.setBounds (margin + 210, y, 180, rowH);
    y += rowH + margin + 8;

    // Left column width: controls fit within ~460 px, leave right for spectrum.
    const int splitX   = 470;
    const int rightX   = splitX + margin;
    const int rightW   = getWidth() - rightX - margin;
    const int bottomY  = getHeight() - 40;

    // --- Plan editor (left column) ---
    planLabel.setBounds (margin, y, 200, 20);
    y += 24;
    gainLabelsLabel .setBounds (margin,                y, labelW, rowH);
    gainLabelsEditor.setBounds (margin + labelW,       y, 260,    rowH);
    buildPlanButton .setBounds (margin + labelW + 268, y, 110,    rowH);
    y += rowH + 8;
    planList.setBounds (margin, y, splitX - margin * 2, bottomY - y);

    // --- Spectrum display (right column) ---
    spectrumDisplay.setBounds (rightX, 8, rightW, bottomY - 8);

    // --- Status bar ---
    statusLabel.setBounds (margin, getHeight() - 28, getWidth() - margin * 2, 24);
}
