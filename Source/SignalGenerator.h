#pragma once
#include <JuceHeader.h>

// Generates standard test signals as in-memory WAV data.
// Pass the returned MemoryBlock to AudioEngine::loadReferenceFileFromMemory().
class SignalGenerator
{
public:
    // Logarithmic sine sweep from 20 Hz to 20 kHz.
    static juce::MemoryBlock makeLogSineSweep (float durationSecs = 10.0f,
                                               double sampleRate  = 44100.0,
                                               float amplitude    = 0.707f);

    // Pink noise (-3 dB/octave spectrum) using the Voss-McCartney algorithm.
    static juce::MemoryBlock makePinkNoise    (float durationSecs = 10.0f,
                                               double sampleRate  = 44100.0,
                                               float amplitude    = 0.25f);

    // Pure sine tone at a given frequency.
    static juce::MemoryBlock makeSineTone     (float frequencyHz,
                                               float durationSecs = 10.0f,
                                               double sampleRate  = 44100.0,
                                               float amplitude    = 0.707f);

private:
    // Encodes a mono float buffer into a WAV MemoryBlock at the given sample rate.
    static juce::MemoryBlock writeMonoWav (const juce::AudioBuffer<float>& buffer,
                                           double sampleRate);
};
