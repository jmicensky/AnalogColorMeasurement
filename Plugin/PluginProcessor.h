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
    // Display-only: updated by loadArtifact on the message thread so the editor
    // can immediately show model info without waiting for the audio thread to
    // consume the pending model.  Never touched by processBlock.
    bool hasModel() const { return displayModelValid; }
    const LNLModel& getModel() const { return displayModel; }

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Circular-buffer direct-form FIR — fallback when block size doesn't match.
    void applyFIR (float* data, int numSamples, std::vector<float>& history, int& writePos,
                   const std::vector<float>& fir);

    // Overlap-save FFT convolution — used when block size matches olsBlockSize.
    void applyFIR_OLS (float* data, int numSamples, std::vector<float>& overlap,
                       const std::vector<float>& spectrum);

    // Rebuilds overlap-save state (filter spectrum + overlap buffers) into live members.
    // Called from prepareToPlay only — loadArtifact builds into PendingModelState.
    void setupOverlapSave (int numChannels, int samplesPerBlock);

    // Table-lookup waveshaper with linear interpolation.
    // Overload takes an explicit table so the multi-model blended table can be used.
    float applyWaveshaper (float x) const;
    static float applyWaveshaperTable (float x, const std::vector<float>& table);

    // Recomputes blendedWaveshaper from the two neighbouring gain models at the
    // given drive position.  Cheap (1024-element lerp); called once per block.
    void updateBlendedWaveshaper (float drive);

    // --- Thread-safe model swap ---
    // loadArtifact (message thread) builds a PendingModelState and stores it here.
    // consumePendingModel (called at the top of processBlock, audio thread) swaps
    // it atomically into the live members.  The SpinLock protects the unique_ptr
    // itself; the atomic flag is the fast-path check.
    struct PendingModelState
    {
        LNLModel                         model;
        std::vector<std::vector<float>>  firHistory;
        std::vector<int>                 firHistoryWritePos;
        std::vector<std::vector<float>>  dryDelayBuffer;
        std::vector<int>                 dryDelayWritePos;
        int                              dryGroupDelay     { 0 };
        std::vector<std::vector<float>>  firSpectra;   // [ch] precomputed FFTs; size 1=mono, 2=stereo
        std::vector<std::vector<float>>  olsOverlap;
        std::unique_ptr<juce::dsp::FFT>  olsFft;
        int                              olsN              { 0 };
        int                              olsOrder          { 0 };
        int                              olsBlockSize      { 0 };
        std::vector<float>               weightLpState;
        std::vector<std::array<float,4>> lowShelfState;
        std::vector<float>               blendedWaveshaper;
        int                              blendLoIdx        { 0 };
        int                              blendHiIdx        { 0 };
        float                            blendT            { 0.0f };
        std::vector<float>               blendedWaveshaperR;
        int                              blendLoIdxR       { 0 };
        int                              blendHiIdxR       { 0 };
        float                            blendTR           { 0.0f };
        float                            weightLpAlpha     { 0.0f };
        // Volterra per-channel circular delay buffer (depth = max(M1,M2)).
        std::vector<std::vector<float>>  volterraDelayBuf;
        std::vector<int>                 volterraDelayWritePos;
    };

    // Called at the top of every processBlock to swap in a pending model if one exists.
    void consumePendingModel();

    std::unique_ptr<PendingModelState> pendingModel;
    juce::SpinLock                     pendingModelLock;
    std::atomic<bool>                  newModelPending { false };

    // Message-thread display copy — set by loadArtifact, read by the editor.
    // The audio thread never accesses these.
    LNLModel displayModel;
    bool     displayModelValid { false };

    LNLModel   model;
    juce::File currentArtifactFile;   // persisted in getStateInformation

    // Playback sample rate, captured in prepareToPlay so loadArtifact can use it.
    double currentSampleRate { 0.0 };

    // Per-channel FIR circular delay-line histories (length = l1Fir.size() - 1).
    std::vector<std::vector<float>> firHistory;
    std::vector<int>                firHistoryWritePos;   // circular write heads

    // Overlap-save FFT convolution state.
    // firSpectra[ch]: precomputed FFT of the zero-padded FIR for that channel.
    // Size 1 = mono model (all channels share [0]); size 2 = stereo model.
    // olsOverlap:  per-channel save buffer of M-1 samples.
    std::vector<std::vector<float>> firSpectra;
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

public:
    // Biquad coefficients (normalised — a0 absorbed into b's, a0=1).
    struct BiquadCoeffs { float b0, b1, b2, a1, a2; };

    // RBJ low-shelf at 80 Hz, shelf-slope S=0.5 (gradual Pultec character).
    // gainDb: boost amount (positive = boost).  Called from both processBlock
    // and the editor's spectrum display so they always match exactly.
    static BiquadCoeffs makeLowShelfCoeffs (float gainDb, double sampleRate);

private:
    // Multi-model blend state — L channel (and mono).
    std::vector<float> blendedWaveshaper;
    int   blendLoIdx { 0 };
    int   blendHiIdx { 0 };
    float blendT     { 0.0f };
    // R channel (stereo models only).
    std::vector<float> blendedWaveshaperR;
    int   blendLoIdxR { 0 };
    int   blendHiIdxR { 0 };
    float blendTR     { 0.0f };

    // Volterra per-channel circular delay buffer.
    // Depth = max(volterraM1, volterraM2); empty for LNL artifacts.
    std::vector<std::vector<float>> volterraDelayBuf;
    std::vector<int>                volterraDelayWritePos;

    // Weight EQ state.
    // Right of centre (weight>0.5): one-pole high shelf at 800 Hz.
    // Left  of centre (weight<0.5): biquad low shelf  at  80 Hz.
    float weightLpAlpha { 0.0f };
    std::vector<float>               weightLpState;
    // Per-channel biquad delay line: [x[n-1], x[n-2], y[n-1], y[n-2]]
    std::vector<std::array<float,4>> lowShelfState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HardwareColorProcessor)
};
