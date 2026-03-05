#include "SessionWriter.h"

juce::String SessionWriter::initialise (const juce::String& name,
                                        const juce::String& modeName)
{
    // Strip characters that are invalid in filesystem paths, then trim whitespace.
    // retainCharacters() keeps only the listed characters so the folder name is
    // safe on macOS, Windows, and Linux.
    juce::String safeName = name
        .retainCharacters ("abcdefghijklmnopqrstuvwxyz"
                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "0123456789_- ")
        .trim();

    if (safeName.isEmpty())
        return "Project name must contain at least one alphanumeric character.";

    // Base directory: ~/Documents/HardwareProfiler/
    // juce::File::userDocumentsDirectory resolves to the platform Documents folder.
    juce::File baseDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                             .getChildFile ("HardwareProfiler");

    juce::File folder = baseDir.getChildFile (safeName);

    // createDirectory() is a no-op if the folder already exists, so re-initialising
    // an existing project is safe — it just opens the existing folder.
    auto result = folder.createDirectory();
    if (! result.wasOk())
        return "Could not create project folder: " + result.getErrorMessage();

    projectName   = safeName;
    mode          = modeName.toLowerCase();
    projectFolder = folder;
    initialised   = true;

    return {};  // empty string = success
}

juce::File SessionWriter::getRefFilePath() const
{
    // Naming scheme: {projectName}_{mode}_ref.wav
    return projectFolder.getChildFile (projectName + "_" + mode + "_ref.wav");
}

juce::File SessionWriter::getRecFilePath() const
{
    // Naming scheme: {projectName}_{mode}_rec.wav
    return projectFolder.getChildFile (projectName + "_" + mode + "_rec.wav");
}
