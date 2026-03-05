#pragma once
#include <JuceHeader.h>

class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

private:
    juce::AudioDeviceManager deviceManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
