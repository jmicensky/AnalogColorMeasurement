#pragma once
#include <JuceHeader.h>
#include "MeasurementTypes.h"

class SessionWriter
{
public:
    SessionWriter() = default;

    // Creates ~/Documents/HardwareProfiler/{projectName}/ and stores paths.
    // Returns an error string on failure, empty string on success.
    juce::String initialise (const juce::String& projectName,
                             const juce::String& mode);

    bool         isInitialised()  const { return initialised; }
    juce::String getProjectName() const { return projectName; }
    juce::String getMode()        const { return mode; }
    juce::File   getProjectFolder() const { return projectFolder; }

    // Legacy single-capture paths (used when no stimulus plan is active):
    //   {projectFolder}/{projectName}_{mode}_ref.wav
    //   {projectFolder}/{projectName}_{mode}_rec.wav
    juce::File getRefFilePath() const;
    juce::File getRecFilePath() const;

    // Per-step paths: {projectName}_{mode}_{gainLabel}_{stimulusName}_ref/rec.wav
    juce::File getStepRefFilePath (const StimulusStep& step) const;
    juce::File getStepRecFilePath (const StimulusStep& step) const;

    // Write session.json into the project folder (overwrites on each call).
    juce::String writeSessionJson (const juce::Array<StimulusStep>& steps,
                                   float sampleRate,
                                   int sendChannel, int returnChannel,
                                   int monitorChannel,
                                   int latencySamples) const;

    // Write markers.json into the project folder (overwrites on each call).
    juce::String writeMarkersJson (const juce::Array<MarkerEntry>& markers) const;

private:
    bool         initialised  { false };
    juce::String projectName;
    juce::String mode;
    juce::File   projectFolder;

    // Strips characters that are unsafe in filenames (keeps alphanumeric, _, +, -, .).
    static juce::String sanitiseLabel (const juce::String& label);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionWriter)
};
