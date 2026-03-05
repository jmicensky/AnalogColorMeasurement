#pragma once
#include <JuceHeader.h>

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

    // Output file paths:
    //   {projectFolder}/{projectName}_{mode}_ref.wav
    //   {projectFolder}/{projectName}_{mode}_rec.wav
    juce::File getRefFilePath() const;
    juce::File getRecFilePath() const;

private:
    bool         initialised  { false };
    juce::String projectName;
    juce::String mode;
    juce::File   projectFolder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionWriter)
};
