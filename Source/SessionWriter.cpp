// Creates and manages the project folder structure on disk, providing per-step WAV file paths.
// Writes session.json (routing, sample rate, step list) and markers.json after each completed capture.

#include "SessionWriter.h"

juce::String SessionWriter::initialise (const juce::String& name,
                                        const juce::String& modeName)
{
    // Strip characters that are invalid in filesystem paths, then trim whitespace.
    juce::String safeName = name
        .retainCharacters ("abcdefghijklmnopqrstuvwxyz"
                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "0123456789_- ")
        .trim();

    if (safeName.isEmpty())
        return "Project name must contain at least one alphanumeric character.";

    juce::File baseDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                             .getChildFile ("HardwareProfiler");

    juce::File folder = baseDir.getChildFile (safeName);

    auto result = folder.createDirectory();
    if (! result.wasOk())
        return "Could not create project folder: " + result.getErrorMessage();

    juce::File archive = baseDir.getChildFile (safeName + "_raw");
    auto archiveResult = archive.createDirectory();
    if (! archiveResult.wasOk())
        return "Could not create archive folder: " + archiveResult.getErrorMessage();

    projectName   = safeName;
    mode          = modeName.toLowerCase();
    projectFolder = folder;
    archiveFolder = archive;
    initialised   = true;

    return {};
}

//==============================================================================
juce::File SessionWriter::getRefFilePath() const
{
    return projectFolder.getChildFile (projectName + "_" + mode + "_ref.wav");
}

juce::File SessionWriter::getRecFilePath() const
{
    return projectFolder.getChildFile (projectName + "_" + mode + "_rec.wav");
}

//==============================================================================
juce::String SessionWriter::sanitiseLabel (const juce::String& label)
{
    // Keep characters that are safe in filenames on macOS/Windows/Linux.
    // + and - are fine in filenames and are common in gain labels (e.g. "+6dBu").
    return label
        .retainCharacters ("abcdefghijklmnopqrstuvwxyz"
                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "0123456789_+-.")
        .trim();
}

juce::File SessionWriter::getStepRefFilePath (const StimulusStep& step) const
{
    const juce::String stem = projectName + "_" + mode
                            + "_" + step.stimulusName + "_ref.wav";
    return projectFolder.getChildFile (sanitiseLabel (step.gainLabel))
                        .getChildFile (stem);
}

juce::File SessionWriter::getStepRecFilePath (const StimulusStep& step) const
{
    const juce::String stem = projectName + "_" + mode
                            + "_" + step.stimulusName + "_rec.wav";
    return projectFolder.getChildFile (sanitiseLabel (step.gainLabel))
                        .getChildFile (stem);
}

juce::File SessionWriter::getStepArchiveRefFilePath (const StimulusStep& step) const
{
    const juce::String stem = projectName + "_" + mode
                            + "_" + step.stimulusName + "_ref.wav";
    return archiveFolder.getChildFile (sanitiseLabel (step.gainLabel))
                        .getChildFile (stem);
}

juce::File SessionWriter::getStepArchiveRecFilePath (const StimulusStep& step) const
{
    const juce::String stem = projectName + "_" + mode
                            + "_" + step.stimulusName + "_rec.wav";
    return archiveFolder.getChildFile (sanitiseLabel (step.gainLabel))
                        .getChildFile (stem);
}

//==============================================================================
juce::String SessionWriter::writeSessionJson (const juce::Array<StimulusStep>& steps,
                                              float sampleRate,
                                              int sendChannel, int returnChannel,
                                              int monitorChannel,
                                              int latencySamples) const
{
    if (! initialised)
        return "Session not initialised.";

    auto* root = new juce::DynamicObject();
    root->setProperty ("projectName",    projectName);
    root->setProperty ("mode",           mode);
    root->setProperty ("date",           juce::Time::getCurrentTime().toISO8601 (true));
    root->setProperty ("sampleRate",     (double) sampleRate);
    root->setProperty ("latencySamples", latencySamples);

    auto* routing = new juce::DynamicObject();
    routing->setProperty ("sendPair",    sendChannel);
    routing->setProperty ("returnPair",  returnChannel);
    routing->setProperty ("monitorPair", monitorChannel);
    root->setProperty ("channelRouting", juce::var (routing));

    juce::Array<juce::var> stepsArr;
    for (int i = 0; i < steps.size(); ++i)
    {
        const auto& s = steps.getReference (i);
        auto* stepObj = new juce::DynamicObject();
        stepObj->setProperty ("index",        i);
        stepObj->setProperty ("gainLabel",    s.gainLabel);
        stepObj->setProperty ("stimulusName", s.stimulusName);
        stepObj->setProperty ("completed",    s.completed);
        stepsArr.add (juce::var (stepObj));
    }
    root->setProperty ("steps", stepsArr);

    const juce::String json = juce::JSON::toString (juce::var (root));
    const juce::File   file = projectFolder.getChildFile ("session.json");

    if (! file.replaceWithText (json))
        return "Could not write session.json.";

    return {};
}

juce::String SessionWriter::writeMarkersJson (const juce::Array<MarkerEntry>& markers) const
{
    if (! initialised)
        return "Session not initialised.";

    juce::Array<juce::var> arr;
    for (const auto& m : markers)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("stepIndex",    m.stepIndex);
        obj->setProperty ("gainLabel",    m.gainLabel);
        obj->setProperty ("stimulusName", m.stimulusName);
        obj->setProperty ("refFile",      m.refFile);
        obj->setProperty ("recFile",      m.recFile);
        obj->setProperty ("timestamp",    m.timestamp);
        arr.add (juce::var (obj));
    }

    const juce::String json = juce::JSON::toString (juce::var (arr));
    const juce::File   file = projectFolder.getChildFile ("markers.json");

    if (! file.replaceWithText (json))
        return "Could not write markers.json.";

    return {};
}
