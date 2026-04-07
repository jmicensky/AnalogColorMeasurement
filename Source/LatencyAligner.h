#pragma once
#include <JuceHeader.h>

class LatencyAligner
{
public:
    // Returns the number of samples that rec lags behind ref, using FFT-based
    // cross-correlation on the first ~2 seconds of captured audio.
    // Hardware round-trip latency is typically <500ms; returns 0 if the result
    // is outside that range or the buffers are too short to analyse.
    static int findLatencySamples (const juce::AudioBuffer<float>& ref,
                                   const juce::AudioBuffer<float>& rec,
                                   int   numCapturedSamples,
                                   float sampleRate);
};
