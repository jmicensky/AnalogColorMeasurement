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

    // Applies the L1 FIR to a single channel.
    void applyFIR (float* data, int numSamples, std::vector<float>& history);

    // Table-lookup waveshaper with linear interpolation.
    float applyWaveshaper (float x) const;

    LNLModel model;

    // Per-channel FIR delay-line histories (length = l1Fir.size() - 1).
    std::vector<std::vector<float>> firHistory;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HardwareColorProcessor)
};
