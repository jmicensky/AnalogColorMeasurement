#pragma once
#include <JuceHeader.h>
#include "LNLModel.h"

// Serialises and deserialises an LNLModel to/from a JSON artifact file.
class ArtifactFile
{
public:
    // Write model to file as pretty-printed JSON.
    // Returns an error string on failure, empty string on success.
    static juce::String save (const LNLModel& model, const juce::File& file);

    // Read model from a JSON artifact file previously written by save().
    // Returns an error string on failure, empty string on success.
    static juce::String load (const juce::File& file, LNLModel& model);
};
