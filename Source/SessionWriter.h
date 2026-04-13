#pragma once
#include <JuceHeader.h>
#include "MeasurementTypes.h"

class SessionWriter
{
public:
    SessionWriter() = default;

    // Creates ~/Documents/HardwareProfiler/{projectName}/ (working folder) and
    // ~/Documents/HardwareProfiler/{projectName}_raw/ (archive folder).
    // Returns an error string on failure, empty string on success.
    juce::String initialise (const juce::String& projectName,
                             const juce::String& mode);

    bool         isInitialised()  const { return initialised; }
    juce::String getProjectName() const { return projectName; }
    juce::String getMode()        const { return mode; }
    juce::File   getProjectFolder() const { return projectFolder; }
    juce::File   getArchiveFolder()  const { return archiveFolder; }

    // Legacy single-capture paths (used when no stimulus plan is active).
    juce::File getRefFilePath() const;
    juce::File getRecFilePath() const;

    // Per-step paths in the working project folder (latency-corrected files).
    juce::File getStepRefFilePath (const StimulusStep& step) const;
    juce::File getStepRecFilePath (const StimulusStep& step) const;

    // Per-step paths in the raw archive folder.
    // Written once, before any latency trimming.  Never touched again.
    juce::File getStepArchiveRefFilePath (const StimulusStep& step) const;
    juce::File getStepArchiveRecFilePath (const StimulusStep& step) const;

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
    juce::File   archiveFolder;

    static juce::String sanitiseLabel (const juce::String& label);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionWriter)
};
