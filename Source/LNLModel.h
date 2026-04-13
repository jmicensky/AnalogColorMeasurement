#pragma once
#include <JuceHeader.h>
#include <vector>

// Result of one THD measurement (one sine tone capture at one gain level).
struct THDEntry
{
    juce::String gainLabel;
    juce::String stimulusName;
    float thdPercent             { 0.0f };
    float fundamentalAmplitudeDb { 0.0f };
};

// Gray-box L–N–L model for a hardware device.
// L1: linear FIR filter applied before the nonlinearity.
// N : static memoryless nonlinearity, stored as a 1024-entry lookup table
//     covering input range [−1, +1].
// L2: linear FIR filter applied after the nonlinearity (identity if empty).
struct LNLModel
{
    int          version    { 1 };
    juce::String deviceName;
    juce::String date;
    double       sampleRate { 44100.0 };

    // Input filter.  512 taps, linear-phase, designed from measured FR.
    // Contains a single [1.0] entry (identity) when no sweep was analysed.
    std::vector<float> l1Fir;

    // Output filter.  Identity (empty) unless explicitly fitted.
    std::vector<float> l2Fir;

    // Waveshaper lookup table: 1024 entries uniformly spaced over [−1, +1].
    // waveshaper[i] = output sample when input = −1 + 2·i/1023.
    // Identity ramp when no tone data is available.
    std::vector<float> waveshaper;

    // Analysis data stored for display and documentation.
    std::vector<float> frFrequencies;  // Hz, one per bin
    std::vector<float> frMagnitudeDb;  // dB, one per bin
    std::vector<THDEntry> thdResults;

    bool isValid() const
    {
        return !l1Fir.empty() && !waveshaper.empty() && sampleRate > 0.0;
    }
};
