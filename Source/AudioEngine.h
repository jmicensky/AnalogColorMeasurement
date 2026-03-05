#pragma once
#include <JuceHeader.h>

class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

    // Called from the UI (message thread) to begin/end tone generation.
    // startMeasurement() validates routing before activating the callback.
    void startMeasurement();
    void stopMeasurement();
    bool isMeasuring() const { return measuring.load(); }

    // Routing — set before calling startMeasurement().
    // firstChannel is the 0-based index of the left channel in the pair.
    void setSendChannelPair    (int firstChannel) { sendChannel   .store (firstChannel); }
    void setMonitorChannelPair (int firstChannel) { monitorChannel.store (firstChannel); }

    // Tone parameters — safe to call at any time including during measurement.
    void setToneFrequency (float freqHz) { toneFreq .store (freqHz);  }
    void setToneLevel     (float dBFS)   { toneLeveldBFS.store (dBFS); }

private:
    // --- AudioIODeviceCallback ---
    void audioDeviceIOCallbackWithContext (
        const float* const* inputChannelData,  int numInputChannels,
        float* const*       outputChannelData, int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext&) override;

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // --- Owned objects ---
    juce::AudioDeviceManager deviceManager;

    // --- Shared state (message thread writes, audio thread reads) ---
    // std::atomic ensures reads/writes are safe across threads without locks.
    std::atomic<bool>  measuring      { false };
    std::atomic<int>   sendChannel    { 0 };
    std::atomic<int>   monitorChannel { -1 };   // -1 = not set
    std::atomic<float> toneFreq       { 1000.0f };
    std::atomic<float> toneLeveldBFS  { -18.0f };

    // --- Audio-thread-only state (no atomic needed) ---
    float phase      { 0.0f };
    float sampleRate { 44100.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
