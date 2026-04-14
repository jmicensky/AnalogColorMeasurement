#pragma once
#include <JuceHeader.h>

class LatencyAligner
{
public:
    // Returns the number of samples (including sub-sample fractional part) that
    // rec lags behind ref.  The integer part is used for sample-accurate trim;
    // the fractional part is passed to applyFractionalDelay for sub-sample
    // correction before writing to disk.
    // Returns 0.0f if the result is outside the expected ±500 ms range or the
    // buffers are too short.
    static float findLatencySamples (const juce::AudioBuffer<float>& ref,
                                     const juce::AudioBuffer<float>& rec,
                                     int   numCapturedSamples,
                                     float sampleRate);

    // Applies a sub-sample fractional delay to every channel of buf using
    // 4-point Lagrange interpolation.  fracDelay must be in [0, 1).
    // Call this after trimRecBuffer() has handled the integer part.
    // No-op if fracDelay < 0.001 (negligible sub-sample error).
    static void applyFractionalDelay (juce::AudioBuffer<float>& buf,
                                      int numSamples,
                                      float fracDelay);
};
