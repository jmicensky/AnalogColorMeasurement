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

// Per-gain-level nonlinearity snapshot.
// Each GainModel captures the waveshaper at one hardware gain-knob position.
// gainValue is normalised 0 (least driven) → 1 (most driven) based on the
// sorted order of captured gain labels.
struct GainModel
{
    juce::String        gainLabel;
    float               gainValue  { 0.5f };
    std::vector<float>  waveshaper;   // 1024-entry table for this gain position
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

    // Input pad applied automatically by the plugin before the waveshaper.
    // Set at analysis time based on the device's expected operating level.
    // 0.0 = line level (no pad).  -18.0 = instrument level (-18 dB pad).
    // Defaults to 0.0 so existing artifacts load without any level change.
    float inputPadDb { 0.0f };

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

    // Per-gain-level models, sorted ascending by gainValue.
    // Empty for single-model artifacts (backward compatible).
    std::vector<GainModel> gainModels;

    bool isValid() const
    {
        return !l1Fir.empty() && !waveshaper.empty() && sampleRate > 0.0;
    }

    bool isMultiModel() const { return gainModels.size() > 1; }
};
