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

    // On macOS, initialise() may pick separate devices for input and output
    // (e.g. system default output = audio interface, default input = built-in
    // microphone). For measurement we need both I/O on the same hardware
    // device. If they differ, force the input device to match the output.
    auto setup = deviceManager.getAudioDeviceSetup();

    if (setup.outputDeviceName.isNotEmpty() &&
        setup.inputDeviceName != setup.outputDeviceName)
    {
        setup.inputDeviceName         = setup.outputDeviceName;
        setup.useDefaultInputChannels = true;
        auto err = deviceManager.setAudioDeviceSetup (setup, true);

        if (err.isNotEmpty())
            DBG ("AudioDeviceManager input alignment error: " + err);
    }
}

AudioEngine::~AudioEngine()
{
    deviceManager.closeAudioDevice();
}
