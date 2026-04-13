#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../Source/ArtifactFile.h"

//==============================================================================
HardwareColorProcessor::HardwareColorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
HardwareColorProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "drive", 1 },
        "Drive",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f),
        1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "weight", 1 },
        "Weight",
        juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f),
        1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 },
        "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        1.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
void HardwareColorProcessor::prepareToPlay (double /*sampleRate*/, int /*samplesPerBlock*/)
{
    // Re-allocate FIR history buffers when model changes or playback restarts.
    const int numCh  = getTotalNumInputChannels();
    const int histLen = model.l1Fir.empty() ? 0 : (int) model.l1Fir.size() - 1;

    firHistory.assign (numCh, std::vector<float> (histLen, 0.0f));
}

//==============================================================================
void HardwareColorProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (! model.isValid())
        return;

    const float drive  = *apvts.getRawParameterValue ("drive");
    const float weight = *apvts.getRawParameterValue ("weight");
    const float mix    = *apvts.getRawParameterValue ("mix");

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // Ensure history is sized correctly (e.g. after a model load).
    const int histLen = model.l1Fir.empty() ? 0 : (int) model.l1Fir.size() - 1;
    if ((int) firHistory.size() != numChannels)
        firHistory.assign (numChannels, std::vector<float> (histLen, 0.0f));

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = buffer.getWritePointer (ch);

        // Store dry signal for mix.
        std::vector<float> dry (data, data + numSamples);

        // --- N: drive + waveshaper ---
        for (int n = 0; n < numSamples; ++n)
        {
            float x = data[n] * drive;
            x = juce::jlimit (-1.0f, 1.0f, x);
            data[n] = applyWaveshaper (x) * weight;
        }

        // --- L2: FIR tone shaping applied post-saturation ---
        // Applied after the waveshaper so it shapes the distorted output rather
        // than pre-filtering the input (which would make the sound thin).
        if (! model.l1Fir.empty())
            applyFIR (data, numSamples, firHistory[ch]);

        // --- Mix dry/wet ---
        if (mix < 1.0f)
        {
            for (int n = 0; n < numSamples; ++n)
                data[n] = dry[n] * (1.0f - mix) + data[n] * mix;
        }
    }
}

//==============================================================================
void HardwareColorProcessor::applyFIR (float* data, int numSamples,
                                        std::vector<float>& history)
{
    const auto& coeffs = model.l1Fir;
    const int   order  = (int) coeffs.size();

    if (order == 0) return;

    for (int n = 0; n < numSamples; ++n)
    {
        // Shift history left and insert new sample.
        for (int k = (int) history.size() - 1; k > 0; --k)
            history[k] = history[k - 1];
        if (! history.empty()) history[0] = data[n];

        // Convolve: y[n] = sum h[k] * x[n-k]
        float y = coeffs[0] * data[n];
        for (int k = 1; k < order && k - 1 < (int) history.size(); ++k)
            y += coeffs[k] * history[k - 1];

        data[n] = y;
    }
}

//==============================================================================
float HardwareColorProcessor::applyWaveshaper (float x) const
{
    const auto& table = model.waveshaper;
    const int   N     = (int) table.size();
    if (N == 0) return x;

    // Map x from [-1, 1] to table index.
    const float idx = (x + 1.0f) * 0.5f * (float) (N - 1);
    const int   i0  = juce::jlimit (0, N - 2, (int) idx);
    const float t   = idx - (float) i0;
    return table[i0] * (1.0f - t) + table[i0 + 1] * t;
}

//==============================================================================
juce::String HardwareColorProcessor::loadArtifact (const juce::File& file)
{
    LNLModel newModel;
    const juce::String err = ArtifactFile::load (file, newModel);
    if (err.isNotEmpty()) return err;

    model = std::move (newModel);

    // Reset FIR histories for all channels.
    const int numCh  = getTotalNumInputChannels();
    const int histLen = model.l1Fir.empty() ? 0 : (int) model.l1Fir.size() - 1;
    firHistory.assign (numCh, std::vector<float> (histLen, 0.0f));

    return {};
}

//==============================================================================
juce::AudioProcessorEditor* HardwareColorProcessor::createEditor()
{
    return new HardwareColorEditor (*this);
}

//==============================================================================
void HardwareColorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void HardwareColorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HardwareColorProcessor();
}
