// Central UI component for the Hardware Profiler app, wiring together project setup,
// audio routing, stimulus plan execution, level metering, spectrum display, and analysis trigger.

#include "MainComponent.h"
#include "SignalGenerator.h"
#include "AnalysisEngine.h"
#include "ArtifactFile.h"
#include <BinaryData.h>

MainComponent::MainComponent()
{
    // Persist toggle/routing settings between sessions.
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName     = "HardwareProfiler";
        opts.filenameSuffix      = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        appProperties.setStorageParameters (opts);
    }

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

    if (auto* props = appProperties.getUserSettings())
        instrumentLevelToggle.setToggleState (
            props->getBoolValue ("instrumentLevel", false), juce::dontSendNotification);
    instrumentLevelToggle.onClick = [this]
    {
        if (auto* props = appProperties.getUserSettings())
        {
            props->setValue ("instrumentLevel", instrumentLevelToggle.getToggleState());
            props->saveIfNeeded();
        }
    };
    addAndMakeVisible (instrumentLevelToggle);

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
    addAndMakeVisible (returnMeter);
    meterTimer.owner = this;
    meterTimer.startTimer (100);

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

    monoModeToggle.setToggleState (false, juce::dontSendNotification);
    monoModeToggle.onStateChange = [this]
    {
        audioEngine.setMonoMode (monoModeToggle.getToggleState());
        // Repopulate combos so they show individual channels (mono) or pairs (stereo).
        populateRoutingCombos();
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

    planPresetCombo.addItem ("Saturation \xe2\x80\x94 Standard  (-18, -12, -6, 0, +3, +6)",  1);
    planPresetCombo.addItem ("Saturation \xe2\x80\x94 Extended  (-24, -18, -12, -9, -6, -3, 0, +3, +6)", 2);
    planPresetCombo.addItem ("Saturation \xe2\x80\x94 Percentage  (0%, 50%, 75%, 90%, 100%)", 3);
    planPresetCombo.addItem ("Compression  (-24, -18, -12, -6, 0, +6, +12)", 4);
    planPresetCombo.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (planPresetCombo);

    buildPlanButton.onClick = [this] { onBuildPlanClicked(); };
    addAndMakeVisible (buildPlanButton);

    planListModel.plan = &stimulusPlan;
    planList.setModel (&planListModel);
    planList.setRowHeight (22);
    addAndMakeVisible (planList);

    // --- Spectrum display ---
    addAndMakeVisible (spectrumDisplay);
    spectrumDisplay.setShowSetupHints (true);

    // --- Status bar ---
    statusLabel.setText (audioEngine.getDeviceStatusString() + "  |  No project initialized.",
                         juce::dontSendNotification);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    // --- Scale buttons ---
    auto setupScaleBtn = [this] (juce::TextButton& btn, float scale)
    {
        btn.setClickingTogglesState (false);
        btn.onClick = [this, &btn, scale]
        {
            uiScale = scale;
            scaleBtn50 .setToggleState (&btn == &scaleBtn50,  juce::dontSendNotification);
            scaleBtn75 .setToggleState (&btn == &scaleBtn75,  juce::dontSendNotification);
            scaleBtn100.setToggleState (&btn == &scaleBtn100, juce::dontSendNotification);
            scaleBtn125.setToggleState (&btn == &scaleBtn125, juce::dontSendNotification);
            setSize (juce::roundToInt (1200 * scale), juce::roundToInt (720 * scale));
        };
        addAndMakeVisible (btn);
    };
    setupScaleBtn (scaleBtn50,  0.50f);
    setupScaleBtn (scaleBtn75,  0.75f);
    setupScaleBtn (scaleBtn100, 1.00f);
    setupScaleBtn (scaleBtn125, 1.25f);
    scaleBtn100.setToggleState (true, juce::dontSendNotification);

    // Register this component as the macOS menu bar model.
    // "Audio Device Settings..." appears under the app name in the menu bar.
    juce::PopupMenu appleExtras;
    appleExtras.addItem (1, "Audio Device Settings...");
    juce::MenuBarModel::setMacMainMenu (this, &appleExtras);

    setSize (1200, 720);
}

MainComponent::~MainComponent()
{
    juce::MenuBarModel::setMacMainMenu (nullptr);
}

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
    owner->returnMeter.setLevels (
        juce::Decibels::decibelsToGain (owner->audioEngine.getReturnPeakDb(), -100.0f),
        juce::Decibels::decibelsToGain (owner->audioEngine.getReturnRmsDb(),  -100.0f));
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
    {
        // User is setting up a new project — unlock routing so they can configure it.
        unlockRouting();
        return;
    }

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

    // Unlock routing so the user can set up channels for this project.
    unlockRouting();
    markers.clear();

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

    // New project — unlock routing so the user can configure channels.
    unlockRouting();
    markers.clear();
    stimulusPlan = StimulusPlan{};
    planList.updateContent();

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

    static const juce::StringArray kPresetCsv {
        "-18, -12, -6, 0, +3, +6",
        "-24, -18, -12, -9, -6, -3, 0, +3, +6",
        "0%, 50%, 75%, 90%, 100%",
        "-24, -18, -12, -6, 0, +6, +12"
    };
    const int presetIdx = planPresetCombo.getSelectedId() - 1;
    const juce::String csv = kPresetCsv[juce::jlimit (0, 3, presetIdx)];

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
    model.inputPadDb = instrumentLevelToggle.getToggleState() ? -18.0f : 0.0f;

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

void MainComponent::menuItemSelected (int menuItemID, int)
{
    if (menuItemID == 1)
        onAudioSettingsClicked();
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


void MainComponent::timerCallback()
{
    if (! audioEngine.isFinished())
        return;

    stopTimer();  // always stop first; restarted below if auto-advancing within a gain level

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
            measureButton.setToggleState (false, juce::dontSendNotification);
            measureButton.setButtonText ("Start Measurement");
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
        auto* device     = audioEngine.getDeviceManager().getCurrentAudioDevice();
        const float sr   = device ? (float) device->getCurrentSampleRate() : 44100.0f;
        const bool monoT = monoModeToggle.getToggleState();
        const int sendCh    = monoT ? (sendCombo.getSelectedId() - 2)
                                    : (sendCombo.getSelectedId() - 2) * 2;
        const int returnCh  = monoT ? (returnCombo.getSelectedId() - 2)
                                    : (returnCombo.getSelectedId() - 2) * 2;
        const int monitorCh = (monoT || monitorCombo.getSelectedId() == 1)
                                  ? -1
                                  : (monitorCombo.getSelectedId() - 2) * 2;

        sessionWriter.writeSessionJson (stimulusPlan.getSteps(), sr, sendCh, returnCh, monitorCh, (int) lag);
        sessionWriter.writeMarkersJson (markers);

        // --- Auto-advance within the same gain level ---
        // If the next step shares the same gain label, load its stimulus and
        // restart immediately without requiring the user to press Start again.
        if (! stimulusPlan.isComplete()
            && stimulusPlan.getCurrentStep()->gainLabel == step.gainLabel)
        {
            const auto* next = stimulusPlan.getCurrentStep();
            const juce::String loadErr = loadStimulusByName (next->stimulusName);
            if (loadErr.isEmpty())
            {
                audioEngine.startMeasurement();
                startTimer (100);
                // Keep button in active state so the user can still abort.
                measureButton.setToggleState (true, juce::dontSendNotification);
                measureButton.setButtonText ("Stop Measurement");
                statusLabel.setText ("Auto: ["
                                     + juce::String (stimulusPlan.currentStepIndex() + 1)
                                     + " / "
                                     + juce::String (stimulusPlan.totalSteps())
                                     + "]  \""
                                     + next->gainLabel + "\"  \xe2\x80\x94  " + next->stimulusName,
                                     juce::dontSendNotification);
                return;
            }
            // Fall through to stop state if stimulus failed to load.
        }

        // --- Gain level complete — stop and wait for user ---
        measureButton.setToggleState (false, juce::dontSendNotification);
        measureButton.setButtonText ("Start Measurement");

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
            statusLabel.setText ("Gain level \""
                                 + step.gainLabel
                                 + "\" done.  Set hardware to \""
                                 + next->gainLabel
                                 + "\" then press Start Measurement.",
                                 juce::dontSendNotification);
        }

        return;
    }

    // --- Legacy single-capture path (no plan active) ---
    measureButton.setToggleState (false, juce::dontSendNotification);
    measureButton.setButtonText ("Start Measurement");

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
    audioEngine.setReturnChannelPair  (returnCh);
    audioEngine.setMonitorChannelPair (monitorCh);
    lockRouting();
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

void MainComponent::lockRouting()
{
    routingLocked = true;
    sendCombo    .setEnabled (false);
    returnCombo  .setEnabled (false);
    monitorCombo .setEnabled (false);
    monoModeToggle.setEnabled (false);
    sendLabel    .setEnabled (false);
    returnLabel  .setEnabled (false);
    monitorLabel .setEnabled (false);
}

void MainComponent::unlockRouting()
{
    routingLocked = false;
    // Repopulate so channel options are fresh and selections reset to (not set),
    // signalling to the user that routing needs to be confirmed for this project.
    populateRoutingCombos();
    // Ensure all controls are enabled (populateRoutingCombos handles monitor enable).
    sendCombo    .setEnabled (true);
    returnCombo  .setEnabled (true);
    monoModeToggle.setEnabled (true);
    sendLabel    .setEnabled (true);
    returnLabel  .setEnabled (true);
}

//==============================================================================
void MainComponent::populateRoutingCombos()
{
    // Snapshot current selections so we can restore them if routing is locked.
    const int prevSendId    = sendCombo   .getSelectedId();
    const int prevReturnId  = returnCombo .getSelectedId();
    const int prevMonitorId = monitorCombo.getSelectedId();

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

    if (routingLocked)
    {
        // Routing is committed — restore previous selections (channel names may have
        // changed if device was reconfigured, but indices stay the same).
        if (prevSendId    > 1) sendCombo   .setSelectedId (prevSendId,    juce::dontSendNotification);
        if (prevReturnId  > 1) returnCombo .setSelectedId (prevReturnId,  juce::dontSendNotification);
        if (prevMonitorId > 1) monitorCombo.setSelectedId (prevMonitorId, juce::dontSendNotification);
    }
    else
    {
        sendCombo   .setSelectedId (1);
        returnCombo .setSelectedId (1);
        monitorCombo.setSelectedId (1);
    }

    monitorCombo.setEnabled (! mono && ! routingLocked);
    monitorLabel.setEnabled (! mono && ! routingLocked);
}

void MainComponent::checkRoutingSafety (FixedComboBox* changed)
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

    const float x0 = 10.0f;
    const float x1 = (float) getWidth() - 10.0f;

    g.setColour (juce::Colours::grey);
    if (sepY1 > 0) g.drawHorizontalLine (sepY1,              x0, x1);
    if (sepY2 > 0) g.drawHorizontalLine (sepY2,              x0, x1);
    if (sepY3 > 0) g.drawHorizontalLine (sepY3,              x0, x1);
    g.drawHorizontalLine (getHeight() - juce::roundToInt (30 * uiScale), x0, x1);
}

void MainComponent::resized()
{
    // All base pixel values are authored at 100% (1200×720) and multiplied by uiScale.
    auto s = [this] (int x) { return juce::roundToInt (x * uiScale); };

    // Update fonts to match current scale everywhere.
    const auto f14 = juce::Font (juce::FontOptions().withHeight (14.0f * uiScale));
    projectLabel .setFont (f14);
    modeLabel    .setFont (f14);
    qualityLabel .setFont (f14);
    routingLabel .setFont (f14);
    planLabel    .setFont (f14);
    planListModel.scale = uiScale;
    planList.setRowHeight (s (22));

    const int margin = s (12);
    const int rowH   = s (28);
    const int labelW = s (160);
    int y = margin;

    // --- Project setup ---
    projectLabel.setBounds (margin,           y, s (80),  rowH);
    projectCombo.setBounds (margin + s (85),  y, s (340), rowH);
    y += rowH + s (6);

    if (projectNameEditor.isVisible())
    {
        projectNameEditor.setBounds (margin + s (85),            y, s (240), rowH);
        initProjectButton.setBounds (margin + s (85) + s (248),  y, s (130), rowH);
        y += rowH + s (6);
    }
    y += s (4);
    sepY1 = y;   // separator: below project section

    // --- Mode selector ---
    modeLabel.setBounds (margin, y, s (200), s (20));
    y += s (22);
    saturationModeButton  .setBounds (margin,          y, s (130), rowH);
    compressorModeButton  .setBounds (margin + s (140), y, s (130), rowH);
    instrumentLevelToggle .setBounds (margin + s (290), y, s (200), rowH);
    y += rowH + s (8);

    // --- Capture quality selector ---
    qualityLabel  .setBounds (margin,           y, s (130), rowH);
    quickButton   .setBounds (margin + s (135), y, s (80),  rowH);
    standardButton.setBounds (margin + s (225), y, s (90),  rowH);
    hifiButton    .setBounds (margin + s (325), y, s (70),  rowH);
    y += rowH + margin + s (6);
    sepY2 = y;   // separator: below mode + quality section

    // --- Routing selector ---
    routingLabel.setBounds (margin, y, s (200), s (20));
    y += s (24);
    sendLabel  .setBounds (margin,           y, labelW,  rowH);
    sendCombo  .setBounds (margin + labelW,  y, s (220), rowH);
    y += rowH + s (6);
    returnLabel.setBounds (margin,           y, labelW,  rowH);
    returnCombo.setBounds (margin + labelW,  y, s (220), rowH);
    y += rowH + s (6);
    monitorLabel.setBounds (margin,           y, labelW,  rowH);
    monitorCombo.setBounds (margin + labelW,  y, s (220), rowH);
    y += rowH + s (6);
    monoModeToggle.setBounds (margin, y, s (260), rowH);
    y += rowH + margin + s (2);
    sepY3 = y;   // separator: below routing + mono section

    // --- Measurement control ---
    measureButton.setBounds (margin,          y, s (200), rowH);
    analyseButton.setBounds (margin + s (210), y, s (180), rowH);
    y += rowH + margin + s (8);

    // Left column: controls fit in ~460 px; right side holds meter + spectrum.
    const int splitX  = s (470);
    const int meterW  = s (42);
    const int meterX  = splitX + margin;
    const int rightX  = meterX + meterW + margin;
    const int rightW  = getWidth() - rightX - margin;
    const int bottomY = getHeight() - s (40);

    // --- Plan editor (left column) ---
    planLabel.setBounds (margin, y, s (200), s (20));
    y += s (24);
    const int presetW = splitX - margin * 2 - s (118);
    planPresetCombo.setBounds (margin,                     y, presetW,  rowH);
    buildPlanButton.setBounds (margin + presetW + s (8),   y, s (110),  rowH);
    y += rowH + s (8);
    planList.setBounds (margin, y, splitX - margin * 2, bottomY - y);

    // --- Vertical level meter ---
    returnMeter.setBounds (meterX, s (8), meterW, bottomY - s (8));

    // --- Spectrum display (right column) ---
    spectrumDisplay.setBounds (rightX, s (8), rightW, bottomY - s (8));

    // --- Scale buttons (bottom right, inside status bar row) ---
    const int btnW   = s (36);
    const int btnH   = s (18);
    const int statusY = getHeight() - s (28);
    const int btnY   = statusY + (s (28) - btnH) / 2;
    int bx = getWidth() - margin;
    bx -= btnW;           scaleBtn125.setBounds (bx, btnY, btnW, btnH);
    bx -= btnW + s (3);   scaleBtn100.setBounds (bx, btnY, btnW, btnH);
    bx -= btnW + s (3);   scaleBtn75 .setBounds (bx, btnY, btnW, btnH);
    bx -= btnW + s (3);   scaleBtn50 .setBounds (bx, btnY, btnW, btnH);

    // --- Status bar (leave room for scale buttons on the right) ---
    const int statusW = bx - margin - s (8);
    statusLabel.setBounds (margin, statusY, statusW, s (24));
    statusLabel.setFont (juce::Font (juce::FontOptions().withHeight (12.0f * uiScale)));
}
