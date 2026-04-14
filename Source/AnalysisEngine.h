#pragma once
#include <JuceHeader.h>
#include <vector>
#include "LNLModel.h"

// Offline analysis pipeline.
// All methods are static; call analyseProjectFolder() as the entry point.
class AnalysisEngine
{
public:
    // Scans the project folder for captured ref/rec WAV pairs, computes the
    // frequency response (from SineSweep captures), THD and waveshaper table
    // (from Sine1kHz / Sine100Hz captures), and fills model.
    // Returns an error string on failure, empty string on success.
    static juce::String analyseProjectFolder (const juce::File& projectFolder,
                                               const juce::String& deviceName,
                                               LNLModel& model);

    // Rebuilds the L1 FIR at a target sample rate from stored frequency-response
    // data (frFrequencies in Hz, frMagnitudeDb in dB).  Call from prepareToPlay
    // whenever the DAW rate differs from the model's capture rate.
    static std::vector<float> rebuildFirAtSampleRate (
        const std::vector<float>& frFreqHz,
        const std::vector<float>& frMagDb,
        double targetSampleRate,
        int numTaps = 128);

    // FFT size used for FIR design — public so the plugin can reference it.
    static constexpr int kDesignN = 4096;

private:
    // --- Per-step analysis ---
    // Computes |H[k]| = |Rec[k]| / |Ref[k]| from one capture pair.
    // Output has designBins = kDesignN/2 + 1 values, linearly interpolated
    // from the native FFT resolution.
    static std::vector<float> computeMagnitudeResponse (
        const juce::AudioBuffer<float>& ref,
        const juce::AudioBuffer<float>& rec,
        int numSamples,
        double sampleRate);

    // Designs a linear-phase FIR of length numTaps from a half-spectrum
    // magnitude array (kDesignN/2 + 1 entries).
    static std::vector<float> designLinearPhaseFIR (
        const std::vector<float>& hMag,
        int numTaps = 128);

    // 1/3-octave smoothing of a half-spectrum magnitude array.
    static void smoothMagnitude (std::vector<float>& hMag, double sampleRate);

    // Computes THD for one sine-tone capture and accumulates waveshaper scatter.
    static void analyseSineTone (
        const juce::AudioBuffer<float>& ref,
        const juce::AudioBuffer<float>& rec,
        int numSamples,
        double sampleRate,
        float fundamentalHz,
        const juce::String& gainLabel,
        const juce::String& stimulusName,
        LNLModel& model);

    // Averages the waveshaper scatter bins and fills model.waveshaper.
    static void finaliseWaveshaper (LNLModel& model);

    // Loads a ref/rec WAV pair from disk into AudioBuffers.
    static bool loadCapturePair (const juce::File& refFile,
                                  const juce::File& recFile,
                                  juce::AudioBuffer<float>& refBuf,
                                  juce::AudioBuffer<float>& recBuf,
                                  int& numSamples,
                                  double& sampleRate);

    // --- Constants ---
    static constexpr int kTableSize = 1024;  // waveshaper table entries

    // --- Waveshaper accumulator (reset before each project analysis) ---
    static std::vector<float> wsAccum;
    static std::vector<int>   wsCounts;
};
