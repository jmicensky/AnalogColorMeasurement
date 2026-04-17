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

    // Per-level Volterra kernels (Volterra artifacts only; empty = use global).
    // Tap counts must match model.volterraM1 / volterraM2.
    std::vector<float>  volterraH1;
    std::vector<float>  volterraH2;

    // Right-channel per-level kernels (stereo artifacts, gainModelsR entries only).
    std::vector<float>  volterraH1R;
    std::vector<float>  volterraH2R;
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

    enum class ModelType { LNL, Volterra };
    ModelType modelType { ModelType::LNL };   // defaults to LNL for all existing artifacts

    // Volterra kernels — present only when modelType == Volterra.
    // h1: linear kernel (m1Taps entries, replaces l1Fir during playback).
    // h2: 2nd-order kernel, flat upper-triangular:
    //     h2[m1,m2] (m1≤m2<m2Taps) at index m1*(2*m2Taps-m1+1)/2 + (m2-m1).
    int                volterraM1 { 0 };
    int                volterraM2 { 0 };
    std::vector<float> volterraH1;   // length volterraM1
    std::vector<float> volterraH2;   // length volterraM2*(volterraM2+1)/2

    // Waveshaper lookup table: 1024 entries uniformly spaced over [−1, +1].
    // waveshaper[i] = output sample when input = −1 + 2·i/1023.
    // Identity ramp when no tone data is available.
    std::vector<float> waveshaper;

    // Analysis data stored for display and documentation.
    std::vector<float> frFrequencies;  // Hz, one per bin (shared between L and R)
    std::vector<float> frMagnitudeDb;  // dB, one per bin — left channel
    std::vector<THDEntry> thdResults;

    // Per-gain-level models, sorted ascending by gainValue.
    // Empty for single-model artifacts (backward compatible).
    std::vector<GainModel> gainModels;

    // ---- Right-channel variants for stereo devices ----
    // All empty for mono devices — the plugin falls back to the L-channel fields above.
    std::vector<float>     l1FirR;
    std::vector<float>     frMagnitudeDbR;
    std::vector<float>     waveshaperR;
    std::vector<float>     volterraH1R;
    std::vector<float>     volterraH2R;
    std::vector<GainModel> gainModelsR;

    bool isStereoModel() const { return !waveshaperR.empty(); }

    bool isValid() const {
        if (modelType == ModelType::Volterra)
            return volterraM1 > 0 && volterraM2 > 0
                && (int)volterraH1.size() == volterraM1
                && (int)volterraH2.size() == volterraM2 * (volterraM2 + 1) / 2
                && !waveshaper.empty() && sampleRate > 0.0;
        return !l1Fir.empty() && !waveshaper.empty() && sampleRate > 0.0;
    }

    bool isMultiModel() const { return gainModels.size() > 1; }
};
