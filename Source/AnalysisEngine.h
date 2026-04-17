#pragma once
#include <JuceHeader.h>
#include <map>
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
        int numTaps = -1);   // -1 = auto-scale with sample rate (~43 Hz LF floor)

    // FFT size used for FIR design — public so the plugin can reference it.
    static constexpr int kDesignN = 4096;

    static juce::String identifyVolterraKernels (
        const juce::AudioBuffer<float>& ref,
        const juce::AudioBuffer<float>& rec,
        int numSamples,
        LNLModel& model,
        double sampleRate,
        int m1Taps = 32,
        int m2Taps = 8,
        int channel = 0);

    // Loads a ref/rec WAV pair from disk into AudioBuffers.
    static bool loadCapturePair (const juce::File& refFile,
                                  const juce::File& recFile,
                                  juce::AudioBuffer<float>& refBuf,
                                  juce::AudioBuffer<float>& recBuf,
                                  int& numSamples,
                                  double& sampleRate);

private:
    // --- Per-step analysis ---
    static void computeCrossSpectrum (
        const juce::AudioBuffer<float>& ref,
        const juce::AudioBuffer<float>& rec,
        int numSamples,
        double sampleRate,
        std::vector<double>& outCrossRe,
        std::vector<double>& outCrossIm,
        std::vector<double>& outRefPower,
        int channel = 0);

    static std::vector<float> designLinearPhaseFIR (
        const std::vector<float>& hMag,
        int numTaps = 127);

    static std::vector<float> designMinimumPhaseFIR (
        const std::vector<float>& hMag,
        int numTaps = 127);

    static void smoothMagnitude (std::vector<float>& hMag, double sampleRate);

    static void analyseSineTone (
        const juce::AudioBuffer<float>& ref,
        const juce::AudioBuffer<float>& rec,
        int numSamples,
        double sampleRate,
        float fundamentalHz,
        const juce::String& gainLabel,
        const juce::String& stimulusName,
        LNLModel& model,
        int channel = 0);

    // channel=0 fills model.waveshaper; channel=1 fills model.waveshaperR.
    static void finaliseWaveshaper (LNLModel& model, int channel = 0);

    // channel=0 fills model.gainModels; channel=1 fills model.gainModelsR.
    static void populateGainModels (LNLModel& model, int channel = 0);

    // --- Constants ---
    static constexpr int kTableSize = 1024;

    static void finaliseWaveshaperInto (std::vector<float>& wsOut,
                                        const std::vector<float>& accum,
                                        const std::vector<int>&   counts);

    // --- Global waveshaper accumulators (L channel) ---
    static std::vector<float> wsAccum;
    static std::vector<int>   wsCounts;

    // --- Per-gain-label waveshaper accumulators (L channel) ---
    static std::map<juce::String, std::vector<float>> perGainWsAccum;
    static std::map<juce::String, std::vector<int>>   perGainWsCounts;

    // --- R-channel waveshaper accumulators ---
    static std::vector<float> wsAccumR;
    static std::vector<int>   wsCountsR;
    static std::map<juce::String, std::vector<float>> perGainWsAccumR;
    static std::map<juce::String, std::vector<int>>   perGainWsCountsR;

    // Identifies Volterra kernels for each gain level that has a sweep capture
    // in its subfolder and stores them into gainModels[i].volterraH1/H2
    // (channel=0) or gainModelsR[i].volterraH1R/H2R (channel=1).
    static void populatePerLevelVolterraKernels (LNLModel& model,
                                                  const juce::File& folder,
                                                  int channel = 0);

    //Cholesky helpers
    static bool choleskyDecompose (std::vector<double>& A, int n);
    static void choleskySolve (const std::vector<double>& R, std::vector<double>& b, int n);
};
