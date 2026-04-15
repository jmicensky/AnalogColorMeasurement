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
        int numTaps = 127);

    // FFT size used for FIR design — public so the plugin can reference it.
    static constexpr int kDesignN = 4096;

private:
    // --- Per-step analysis ---
    // Offline Wiener-filter estimation of the transfer function.
    // Computes the complex cross-spectrum conj(X)·Y and the reference power |X|²,
    // both resampled to designBins resolution.
    //
    // Accumulating these across N sweeps before dividing gives *coherent* averaging:
    // uncorrelated noise cancels, so SNR grows as N rather than √N.  This is
    // equivalent to running NLMS with a vanishingly small step size (i.e. fully
    // converged to the Wiener optimum) for each frequency band simultaneously.
    static void computeCrossSpectrum (
        const juce::AudioBuffer<float>& ref,
        const juce::AudioBuffer<float>& rec,
        int numSamples,
        double sampleRate,
        std::vector<double>& outCrossRe,    // Re(conj(X)·Y), designBins
        std::vector<double>& outCrossIm,    // Im(conj(X)·Y), designBins
        std::vector<double>& outRefPower);  // |X|²,          designBins

    // Designs a linear-phase FIR of length numTaps from a half-spectrum
    // magnitude array (kDesignN/2 + 1 entries).
    static std::vector<float> designLinearPhaseFIR (
        const std::vector<float>& hMag,
        int numTaps = 127);

    // Designs a minimum-phase FIR of length numTaps from the same half-spectrum.
    // Uses the cepstral method (Oppenheim & Schafer ch.12): log-mag → IFFT →
    // causal liftering → FFT → exp → IFFT.  No pre-ringing — all energy is
    // causal, matching the transient behaviour of real analog hardware.
    static std::vector<float> designMinimumPhaseFIR (
        const std::vector<float>& hMag,
        int numTaps = 127);

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

    // Shared waveshaper finalisation logic.  Fills wsOut from raw accumulator
    // data using the same interpolation, extrapolation and slope-normalisation
    // steps as the global finaliseWaveshaper.
    static void finaliseWaveshaperInto (std::vector<float>& wsOut,
                                        const std::vector<float>& accum,
                                        const std::vector<int>&   counts);

    // Builds model.gainModels from the per-gain accumulators collected during
    // analyseSineTone.  Called once after finaliseWaveshaper.
    static void populateGainModels (LNLModel& model);

    // --- Global waveshaper accumulator (reset before each project analysis) ---
    static std::vector<float> wsAccum;
    static std::vector<int>   wsCounts;

    // --- Per-gain-label waveshaper accumulators ---
    static std::map<juce::String, std::vector<float>> perGainWsAccum;
    static std::map<juce::String, std::vector<int>>   perGainWsCounts;
};
