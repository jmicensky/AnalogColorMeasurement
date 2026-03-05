#include "AudioEngine.h"

AudioEngine::AudioEngine()
{
    // Initialise the device manager requesting up to 32 input and 32 output
    // channels so that all channels on a multi-channel interface are available
    // for routing. JUCE will open the system default device and clamp the
    // channel count to what the hardware actually supports.
    //
    // Signature:
    //   initialise(numInputChannelsNeeded,
    //              numOutputChannelsNeeded,
    //              savedState,               // nullptr = use defaults
    //              selectDefaultDeviceOnFailure)
    auto result = deviceManager.initialise (32, 32, nullptr, true);

    if (result.isNotEmpty())
        DBG ("AudioDeviceManager initialise error: " + result);
}

AudioEngine::~AudioEngine()
{
    deviceManager.closeAudioDevice();
}
