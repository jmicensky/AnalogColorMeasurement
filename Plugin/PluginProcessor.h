#pragma once
#include <JuceHeader.h>
#include "../Source/LNLModel.h"

// AudioProcessor that applies a loaded LNL artifact to incoming audio.
// Controls: Drive (pre-gain into waveshaper), Mix (dry/wet), Weight (post-gain).
class HardwareColorProcessor : public juce::AudioProcessor
{
public:
    HardwareColorProcessor();
    ~HardwareColorProcessor() override = default;

    //==========================================================================
    const juce::String getName() const override { return "Hardware Color"; }
    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==========================================================================
    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Load an artifact file.  Call from the message thread.
    // Returns an error string on failure, empty string on success.
    juce::String loadArtifact (const juce::File& file);
    bool hasModel() const { return model.isValid(); }
    const LNLModel& getModel() const { return model; }

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Circular-buffer direct-form FIR — fallback when block size doesn't match.
    void applyFIR (float* data, int numSamples, std::vector<float>& history, int& writePos);

    // Overlap-save FFT convolution — used when block size matches olsBlockSize.
    void applyFIR_OLS (float* data, int numSamples, std::vector<float>& overlap);

    // Rebuilds overlap-save state (filter spectrum + overlap buffers).
    // Called from prepareToPlay and loadArtifact.
    void setupOverlapSave (int numChannels, int samplesPerBlock);

    // Table-lookup waveshaper with linear interpolation.
    float applyWaveshaper (float x) const;

    LNLModel model;

    // Playback sample rate, captured in prepareToPlay so loadArtifact can use it.
    double currentSampleRate { 0.0 };

    // Per-channel FIR circular delay-line histories (length = l1Fir.size() - 1).
    std::vector<std::vector<float>> firHistory;
    std::vector<int>                firHistoryWritePos;   // circular write heads

    // Overlap-save FFT convolution state.
    // firSpectrum: precomputed FFT of the zero-padded FIR (2*olsN interleaved).
    // olsOverlap:  per-channel save buffer of M-1 samples.
    std::vector<float>              firSpectrum;
    std::vector<std::vector<float>> olsOverlap;
    std::unique_ptr<juce::dsp::FFT> olsFft;
    int                             olsN         { 0 };  // FFT size
    int                             olsOrder     { 0 };  // log2(olsN)
    int                             olsBlockSize { 0 };  // expected block size B

    // Dry signal delay line — delays dry by the FIR group delay so dry/wet
    // mix is phase-coherent at all frequencies.
    std::vector<std::vector<float>> dryDelayBuffer;
    std::vector<int>                dryDelayWritePos;
    int                             dryGroupDelay { 0 };

    // Weight high-shelf: one-pole LP at 800 Hz, ±6 dB shelf gain.
    // weight=0.5 → hardware-accurate; <0.5 → darker; >0.5 → brighter.
    float weightLpAlpha { 0.0f };
    std::vector<float> weightLpState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HardwareColorProcessor)
};
