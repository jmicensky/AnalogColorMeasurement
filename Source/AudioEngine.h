#pragma once
#include <JuceHeader.h>

class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

    // Load a reference WAV/AIFF file from disk or from embedded binary data.
    // Returns an error string on failure, empty string on success.
    juce::String loadReferenceFile (const juce::File& file);
    juce::String loadReferenceFileFromMemory (const void* data, size_t dataSize,
                                              const juce::String& stem);

    // Returns the stem of the loaded reference file (no extension), e.g. "BASSDI_testtone".
    // Empty if no file is loaded.
    juce::String getReferenceFileStem() const { return referenceFileStem; }

    // Called from the UI (message thread) to begin/end file playback + recording.
    void startMeasurement();
    void stopMeasurement();
    bool isMeasuring() const { return measuring.load(); }

    // Returns true once the reference file has played to its end and measurement
    // has been stopped automatically. Reset on next startMeasurement().
    bool isFinished() const { return finished.load(); }

    // Routing — set before calling startMeasurement().
    // firstChannel is the 0-based index of the left channel in the pair (stereo)
    // or the single channel index (mono).
    void setSendChannelPair    (int firstChannel) { sendChannel   .store (firstChannel); }
    void setMonitorChannelPair (int firstChannel) { monitorChannel.store (firstChannel); }
    void setReturnChannelPair  (int firstChannel) { returnChannel .store (firstChannel); }

    // When mono mode is on, the engine uses a single send channel and a single
    // return channel rather than stereo pairs.
    void setMonoMode (bool mono) { monoMode.store (mono); }
    bool getMonoMode() const     { return monoMode.load(); }

    // Write ref.wav and rec.wav into the session folder.
    // Call this from the message thread after stopMeasurement().
    // Returns an error string on failure, empty string on success.
    juce::String writeSession (const juce::File& refFilePath,
                               const juce::File& recFilePath);

    // Post-capture access (message thread only, call after stopMeasurement()).
    const juce::AudioBuffer<float>& getReferenceBuffer() const { return refBuffer; }
    const juce::AudioBuffer<float>& getRecordBuffer()    const { return recBuffer; }
    int   getCapturePosition() const { return capturePosition; }
    float getSampleRate()      const { return sampleRate; }

    // Integer-trims rec by floor(lagSamples) and then applies a sub-sample
    // fractional advance (via LatencyAligner::applyFractionalDelay) for the
    // remaining frac(lagSamples) portion.  Pass the float returned by
    // LatencyAligner::findLatencySamples directly.
    void trimRecBuffer (float lagSamples);

    // Current peak level (dBFS) of the configured return channel.
    // Updated continuously by the audio callback — safe to call from any thread.
    float getReturnPeakDb() const;

    // Persist audio device settings (device, sample rate, buffer size).
    // Call after the user closes the audio settings dialog.
    void saveDeviceSettings() const;

    // Returns a human-readable "Name  |  48000 Hz  |  512" string for the status bar.
    juce::String getDeviceStatusString() const;

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
    juce::AudioFormatManager formatManager;

    // --- Reference file playback (message-thread write, audio-thread read via ptr) ---
    // ResamplingAudioSource wraps the reader and converts from the file's native
    // sample rate to the device's sample rate on the fly.
    std::unique_ptr<juce::AudioFormatReader>       referenceReader;
    std::unique_ptr<juce::AudioFormatReaderSource> referenceReaderSource;
    std::unique_ptr<juce::ResamplingAudioSource>   resamplingSource;
    juce::String referenceFileStem;
    int          referenceNumChannels { 0 };

    // --- Capture buffers (audio-thread write, message-thread read after stop) ---
    // refBuffer  : the resampled playback frames (what went out)
    // recBuffer  : the raw Return input frames   (what came back)
    // Both are pre-allocated to maxRecordSamples before startMeasurement().
    static constexpr int maxRecordSeconds { 360 };   // 6-minute hard cap
    juce::AudioBuffer<float> refBuffer;
    juce::AudioBuffer<float> recBuffer;
    int capturePosition { 0 };   // audio-thread write head (samples written so far)

    // --- Shared state (message thread writes, audio thread reads) ---
    std::atomic<bool>  measuring      { false };
    std::atomic<bool>  finished       { false };
    std::atomic<bool>  monoMode       { false };
    std::atomic<int>   sendChannel    { 0 };
    std::atomic<int>   monitorChannel { -1 };   // -1 = not set
    std::atomic<int>   returnChannel  { 0 };
    std::atomic<float> returnPeakLinear { 0.0f };

    // Returns ~/Library/Application Support/HardwareProfiler/audioSettings.xml
    static juce::File getSettingsFile();

    // --- Audio-thread-only state ---
    float sampleRate { 44100.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
