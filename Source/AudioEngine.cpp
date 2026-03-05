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

    // Register this object as the audio callback. The callback is always
    // registered but does nothing until measuring == true.
    deviceManager.addAudioCallback (this);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

//==============================================================================
void AudioEngine::startMeasurement()
{
    phase = 0.0f;         // reset phase so every session starts at the same point
    measuring.store (true);
}

void AudioEngine::stopMeasurement()
{
    measuring.store (false);
}

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // Called on the message thread just before the audio device starts.
    // Capture the sample rate here so the callback can use it safely.
    sampleRate = static_cast<float> (device->getCurrentSampleRate());
    phase      = 0.0f;
}

void AudioEngine::audioDeviceStopped()
{
    measuring.store (false);
}

//==============================================================================
void AudioEngine::audioDeviceIOCallbackWithContext (
    const float* const* /*inputChannelData*/,  int /*numInputChannels*/,
    float* const*         outputChannelData,   int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    // Zero all output channels first so unused channels stay silent.
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

    if (!measuring.load())
        return;

    const int   sendCh  = sendChannel.load();
    const int   monCh   = monitorChannel.load();   // -1 if not set
    const float freq    = toneFreq.load();
    const float levelDb = toneLeveldBFS.load();

    // A = 10^(dBFS / 20)  — converts dBFS level to linear amplitude
    // (proposal eq. 3: A = 10^(dBFS/20))
    const float A = std::pow (10.0f, levelDb / 20.0f);

    // Phase increment per sample:  Δφ = 2π · f₀ / fₛ
    // Used in the phase-accumulator form of:
    //   x[n] = A · sin(2π · f₀/fₛ · n + φ₀)   (proposal eq. 4)
    // Accumulating phase rather than computing (2π·f₀/fₛ·n) directly
    // avoids floating-point precision loss as n grows large.
    const float delta = juce::MathConstants<float>::twoPi * freq / sampleRate;

    for (int n = 0; n < numSamples; ++n)
    {
        const float sample = A * std::sin (phase);
        phase += delta;

        // Write to Send pair (left = sendCh, right = sendCh + 1)
        if (sendCh >= 0 && sendCh < numOutputChannels)
        {
            if (outputChannelData[sendCh] != nullptr)
                outputChannelData[sendCh][n] = sample;

            if (sendCh + 1 < numOutputChannels && outputChannelData[sendCh + 1] != nullptr)
                outputChannelData[sendCh + 1][n] = sample;
        }

        // Mirror to Monitor pair so the user can hear the stimulus.
        // Guard ensures Monitor never overlaps Send (safety constraint is
        // already enforced in the UI, this is a belt-and-suspenders check).
        if (monCh >= 0 && monCh != sendCh && monCh < numOutputChannels)
        {
            if (outputChannelData[monCh] != nullptr)
                outputChannelData[monCh][n] = sample;

            if (monCh + 1 < numOutputChannels && outputChannelData[monCh + 1] != nullptr)
                outputChannelData[monCh + 1][n] = sample;
        }
    }

    // Wrap phase to [0, 2π] after each buffer to prevent unbounded growth.
    phase = std::fmod (phase, juce::MathConstants<float>::twoPi);
}
