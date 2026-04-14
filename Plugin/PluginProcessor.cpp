#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../Source/ArtifactFile.h"
#include "../Source/AnalysisEngine.h"

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

    // Logarithmic skew (0.5) maps the knob's 50% position to Drive = 2.0.
    // The lower half covers 0–2.0 with fine resolution for subtle saturation;
    // the upper half pushes into heavy drive up to 10.0.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "drive", 1 },
        "Drive",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f, 0.5f),
        1.0f));

    // Weight is a tilt EQ on the wet saturation path.
    // 0.5 = flat (default); <0.5 = LF saturation emphasis; >0.5 = HF saturation emphasis.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "weight", 1 },
        "Weight",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 },
        "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "outputGain", 1 },
        "Output",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f),
        6.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
void HardwareColorProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // Always rebuild the FIR from stored frequency-response data at the current
    // sample rate.  This ensures the correct (odd) tap count and the latest
    // design improvements (taper, regularisation) are applied regardless of
    // what tap count was saved in the artifact.
    if (model.isValid() && ! model.frFrequencies.empty())
    {
        model.l1Fir = AnalysisEngine::rebuildFirAtSampleRate (
                          model.frFrequencies, model.frMagnitudeDb, sampleRate);
    }

    const int numCh   = getTotalNumInputChannels();
    const int histLen = model.l1Fir.empty() ? 0 : (int) model.l1Fir.size() - 1;
    firHistory.assign (numCh, std::vector<float> (histLen, 0.0f));
    firHistoryWritePos.assign (numCh, 0);

    // Dry delay line: compensates the FIR's group delay so dry/wet mix is
    // phase-coherent.  Group delay of a linear-phase FIR = (taps - 1) / 2.
    dryGroupDelay = model.l1Fir.empty() ? 0 : ((int) model.l1Fir.size() - 1) / 2;
    const int dlyLen = std::max (1, dryGroupDelay);
    dryDelayBuffer.assign (numCh, std::vector<float> (dlyLen, 0.0f));
    dryDelayWritePos.assign (numCh, 0);

    setLatencySamples (dryGroupDelay);

    // Overlap-save FFT convolution setup.
    setupOverlapSave (numCh, samplesPerBlock);

    // Weight EQ setup.
    // Right of centre: one-pole high shelf at 800 Hz (brighter).
    // Left  of centre: biquad low shelf at  80 Hz (richer/warmer, Pultec-style).
    weightLpAlpha = std::exp (-juce::MathConstants<float>::twoPi * 800.0f / (float) sampleRate);
    weightLpState.assign (numCh, 0.0f);
    lowShelfState.assign (numCh, {});
}

//==============================================================================
void HardwareColorProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (! model.isValid())
        return;

    const float drive      = *apvts.getRawParameterValue ("drive");
    const float weight     = *apvts.getRawParameterValue ("weight");
    const float mix        = *apvts.getRawParameterValue ("mix");
    const float outputGain = juce::Decibels::decibelsToGain (
                                 apvts.getRawParameterValue ("outputGain")->load());

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // Pre-compute interpolated waveshaper for multi-model (once per block).
    if (model.isMultiModel())
        updateBlendedWaveshaper (drive);

    // Ensure histories are sized correctly (e.g. after a model load).
    const int histLen = model.l1Fir.empty() ? 0 : (int) model.l1Fir.size() - 1;
    if ((int) firHistory.size() != numChannels)
    {
        firHistory.assign (numChannels, std::vector<float> (histLen, 0.0f));
        firHistoryWritePos.assign (numChannels, 0);
    }
    if ((int) dryDelayBuffer.size() != numChannels)
    {
        const int dlyLen = std::max (1, dryGroupDelay);
        dryDelayBuffer.assign (numChannels, std::vector<float> (dlyLen, 0.0f));
        dryDelayWritePos.assign (numChannels, 0);
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = buffer.getWritePointer (ch);

        // Push current samples into the dry delay line and read out the
        // phase-aligned dry (delayed by the FIR group delay).
        std::vector<float> dry (numSamples);
        if (dryGroupDelay > 0)
        {
            auto& dlBuf = dryDelayBuffer[ch];
            int&  dlPos = dryDelayWritePos[ch];
            const int dlLen = (int) dlBuf.size();
            for (int n = 0; n < numSamples; ++n)
            {
                dry[n]       = dlBuf[dlPos];   // oldest sample = delayed dry
                dlBuf[dlPos] = data[n];        // overwrite with current
                dlPos = (dlPos + 1) % dlLen;
            }
        }
        else
        {
            std::copy (data, data + numSamples, dry.begin());
        }

        // --- N: waveshaper ---
        if (model.isMultiModel())
        {
            // Drive selects which captured gain-level model to use (0 = lightest,
            // 10 = heaviest).  The blended table is recomputed once per block above
            // the channel loop.  No amplitude pre-scaling: the waveshaper at each
            // gain position already encodes the correct hardware transfer curve.
            for (int n = 0; n < numSamples; ++n)
                data[n] = applyWaveshaperTable (data[n], blendedWaveshaper);
        }
        else
        {
            // Single-model (legacy): Drive is a pre-amplification multiplier.
            // Makeup gain keeps output level consistent as Drive changes.
            // Clamped at 0.25 (4× max boost) so near-zero Drive fades gracefully.
            const float makeupGain = 1.0f / std::max (drive, 0.25f);
            for (int n = 0; n < numSamples; ++n)
            {
                const float x = data[n] * drive;
                data[n] = applyWaveshaper (x) * makeupGain;
            }
        }

        // --- L1 FIR tone shaping applied post-saturation ---
        // Use overlap-save FFT convolution when the block size matches the
        // pre-computed FFT size; otherwise fall back to the circular-buffer
        // direct-form (e.g. variable-block-size hosts).
        if (! model.l1Fir.empty())
        {
            if (olsN > 0 && numSamples == olsBlockSize && ch < (int) olsOverlap.size())
                applyFIR_OLS (data, numSamples, olsOverlap[ch]);
            else
                applyFIR (data, numSamples, firHistory[ch], firHistoryWritePos[ch]);
        }

        // --- Weight EQ ---
        // weight=0.5 → hardware-accurate (flat)
        // weight>0.5 → one-pole high-shelf boost at 800 Hz (+6 dB max, brighter)
        // weight<0.5 → biquad low-shelf boost at 80 Hz (+6 dB max, Pultec-style warmer)
        if ((int) weightLpState.size()  != numChannels)  weightLpState.assign  (numChannels, 0.0f);
        if ((int) lowShelfState.size()  != numChannels)  lowShelfState.assign  (numChannels, {});

        if (weight > 0.5f)
        {
            // High-shelf boost only — gain > 1 at all weight > 0.5.
            const float shelfGain = std::pow (10.0f, (weight - 0.5f) * 0.6f);  // 1–2×, 0–+6 dB
            float& lpS = weightLpState[ch];
            for (int n = 0; n < numSamples; ++n)
            {
                lpS      = (1.0f - weightLpAlpha) * data[n] + weightLpAlpha * lpS;
                data[n]  = lpS + shelfGain * (data[n] - lpS);
            }
        }
        else if (weight < 0.5f)
        {
            // Low-shelf boost — gain scales from 0 dB at centre to +6 dB at weight=0.
            const float gainDb = (0.5f - weight) * 12.0f;
            const auto [b0, b1, b2, a1c, a2c] = makeLowShelfCoeffs (gainDb, currentSampleRate);
            auto& s = lowShelfState[ch];   // s = {xn1, xn2, yn1, yn2}
            for (int n = 0; n < numSamples; ++n)
            {
                const float xn = data[n];
                const float yn = b0*xn + b1*s[0] + b2*s[1] - a1c*s[2] - a2c*s[3];
                s[1] = s[0];  s[0] = xn;
                s[3] = s[2];  s[2] = yn;
                data[n] = yn;
            }
        }

        // --- Mix dry/wet ---
        for (int n = 0; n < numSamples; ++n)
            data[n] = dry[n] * (1.0f - mix) + data[n] * mix;

        // --- Output gain ---
        if (outputGain != 1.0f)
            juce::FloatVectorOperations::multiply (data, outputGain, numSamples);
    }
}

//==============================================================================
void HardwareColorProcessor::applyFIR (float* data, int numSamples,
                                        std::vector<float>& history,
                                        int& writePos)
{
    const auto& coeffs = model.l1Fir;
    const int   order  = (int) coeffs.size();
    const int   hLen   = (int) history.size();  // = order - 1

    if (order == 0) return;

    for (int n = 0; n < numSamples; ++n)
    {
        // Write new sample into the circular history buffer.
        // history holds the past (order-1) input samples.
        if (hLen > 0)
        {
            history[writePos] = data[n];
            writePos = (writePos + 1) % hLen;
        }

        // Convolve: y[n] = h[0]*x[n] + h[1]*x[n-1] + ... using circular reads.
        float y = coeffs[0] * data[n];
        for (int k = 1; k < order; ++k)
        {
            const int idx = (writePos - k + hLen) % hLen;
            y += coeffs[k] * history[idx];
        }

        data[n] = y;
    }
}

//==============================================================================
float HardwareColorProcessor::applyWaveshaperTable (float x,
                                                     const std::vector<float>& table)
{
    const int N = (int) table.size();
    if (N == 0) return x;

    const float idx = (x + 1.0f) * 0.5f * (float) (N - 1);
    const int   i0  = juce::jlimit (0, N - 2, (int) idx);
    const float t   = juce::jlimit (0.0f, 1.0f, idx - (float) i0);
    return table[i0] * (1.0f - t) + table[i0 + 1] * t;
}

float HardwareColorProcessor::applyWaveshaper (float x) const
{
    return applyWaveshaperTable (x, model.waveshaper);
}

//==============================================================================
// Interpolate between the two neighbouring gain models at the given drive
// position (0…10 mapped linearly across the gainModels array).
// The result is cached; the 1024-element lerp only re-runs when lo/hi or t changes.
void HardwareColorProcessor::updateBlendedWaveshaper (float drive)
{
    const int N = (int) model.gainModels.size();
    if (N < 2) return;

    const float pos = juce::jlimit (0.0f, (float) (N - 1),
                                    drive / 10.0f * (float) (N - 1));
    const int   lo  = juce::jlimit (0, N - 2, (int) pos);
    const int   hi  = lo + 1;
    const float t   = pos - (float) lo;

    // Skip rebuild if nothing has changed.
    if (lo == blendLoIdx && hi == blendHiIdx
        && std::abs (t - blendT) < 0.0005f
        && ! blendedWaveshaper.empty())
        return;

    blendLoIdx = lo;
    blendHiIdx = hi;
    blendT     = t;

    const auto& wsLo = model.gainModels[lo].waveshaper;
    const auto& wsHi = model.gainModels[hi].waveshaper;
    const int   wsN  = (int) wsLo.size();

    if ((int) wsHi.size() != wsN)   // safety: mismatched table sizes
    {
        blendedWaveshaper = wsLo;
        return;
    }

    blendedWaveshaper.resize (wsN);
    for (int i = 0; i < wsN; ++i)
        blendedWaveshaper[i] = wsLo[i] * (1.0f - t) + wsHi[i] * t;
}

//==============================================================================
void HardwareColorProcessor::setupOverlapSave (int numChannels, int samplesPerBlock)
{
    olsBlockSize = samplesPerBlock;

    if (model.l1Fir.empty() || samplesPerBlock <= 0)
    {
        olsN = 0;
        olsFft.reset();
        firSpectrum.clear();
        olsOverlap.clear();
        return;
    }

    const int M = (int) model.l1Fir.size();

    // N = next power of 2 >= B + M - 1.
    int ord = 1;
    while ((1 << ord) < samplesPerBlock + M - 1)
        ++ord;

    olsOrder = ord;
    olsN     = 1 << ord;
    olsFft   = std::make_unique<juce::dsp::FFT> (ord);

    // Precompute FFT of zero-padded FIR.  Stored in JUCE's interleaved [re,im] layout.
    firSpectrum.assign (2 * olsN, 0.0f);
    for (int k = 0; k < M; ++k)
        firSpectrum[k] = model.l1Fir[k];
    olsFft->performRealOnlyForwardTransform (firSpectrum.data());

    // Per-channel overlap save buffers (M-1 zeros on init / model load).
    olsOverlap.assign (numChannels, std::vector<float> (M - 1, 0.0f));
}

//==============================================================================
void HardwareColorProcessor::applyFIR_OLS (float* data, int numSamples,
                                            std::vector<float>& overlap)
{
    const int M   = (int) model.l1Fir.size();
    const int ovL = M - 1;    // overlap length

    // Build the N-sample input segment: [overlap | new_input] zero-padded to olsN.
    std::vector<float> seg (2 * olsN, 0.0f);
    for (int k = 0; k < ovL; ++k)
        seg[k] = overlap[k];
    for (int k = 0; k < numSamples; ++k)
        seg[ovL + k] = data[k];

    // Save the last M-1 input samples as the new overlap for the next block.
    for (int k = 0; k < ovL; ++k)
        overlap[k] = data[numSamples - ovL + k >= 0 ? numSamples - ovL + k : 0];

    // Forward FFT of the input segment.
    olsFft->performRealOnlyForwardTransform (seg.data());

    // Pointwise complex multiply with the precomputed filter spectrum.
    for (int k = 0; k < olsN; ++k)
    {
        const float re_x = seg[2 * k],         im_x = seg[2 * k + 1];
        const float re_h = firSpectrum[2 * k],  im_h = firSpectrum[2 * k + 1];
        seg[2 * k]     = re_x * re_h - im_x * im_h;
        seg[2 * k + 1] = re_x * im_h + im_x * re_h;
    }

    // Inverse FFT — JUCE normalises by 1/N internally.
    olsFft->performRealOnlyInverseTransform (seg.data());

    // Discard the first M-1 samples (circular aliasing); copy B valid samples out.
    for (int n = 0; n < numSamples; ++n)
        data[n] = seg[ovL + n];
}

//==============================================================================
// RBJ low-shelf biquad at 80 Hz.  Shelf slope S=0.5 gives a broad, gradual
// transition that evokes vintage transformer saturation rather than a surgical
// digital shelf.  gainDb is the boost at DC (positive = boost only).
HardwareColorProcessor::BiquadCoeffs
HardwareColorProcessor::makeLowShelfCoeffs (float gainDb, double sampleRate)
{
    // A = sqrt(linear gain), per the RBJ cookbook.
    const float A   = std::pow (10.0f, gainDb / 40.0f);
    const float w0  = juce::MathConstants<float>::twoPi * 80.0f / (float) sampleRate;
    const float cw  = std::cos (w0);
    const float sw  = std::sin (w0);
    constexpr float S = 0.5f;   // shelf slope: <1 = more gradual

    // alpha term controls the shelf transition width.
    const float alp = sw * 0.5f
                      * std::sqrt ((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);
    const float sqA = std::sqrt (A);

    const float b0r =     A * ((A + 1.0f) - (A - 1.0f) * cw + 2.0f * sqA * alp);
    const float b1r = 2.0f*A * ((A - 1.0f) - (A + 1.0f) * cw);
    const float b2r =     A * ((A + 1.0f) - (A - 1.0f) * cw - 2.0f * sqA * alp);
    const float a0r =         (A + 1.0f) + (A - 1.0f) * cw + 2.0f * sqA * alp;
    const float a1r =  -2.0f * ((A - 1.0f) + (A + 1.0f) * cw);
    const float a2r =          (A + 1.0f) + (A - 1.0f) * cw - 2.0f * sqA * alp;

    return { b0r/a0r, b1r/a0r, b2r/a0r, a1r/a0r, a2r/a0r };
}

//==============================================================================
juce::String HardwareColorProcessor::loadArtifact (const juce::File& file)
{
    LNLModel newModel;
    const juce::String err = ArtifactFile::load (file, newModel);
    if (err.isNotEmpty()) return err;

    model = std::move (newModel);

    // Always rebuild the FIR so the correct odd tap count and latest design
    // improvements are applied regardless of what was saved in the artifact.
    if (! model.frFrequencies.empty())
    {
        const double rate = (currentSampleRate > 0.0) ? currentSampleRate : model.sampleRate;
        model.l1Fir = AnalysisEngine::rebuildFirAtSampleRate (
                          model.frFrequencies, model.frMagnitudeDb, rate);
    }

    // Backwards compatibility: if the artifact pre-dates the frFrequencies field,
    // synthesize display data from the FIR's magnitude response so the spectrum
    // display is populated even without re-running the analysis.
    if (model.frFrequencies.empty() && model.l1Fir.size() > 1)
    {
        const double sr    = (currentSampleRate > 0.0) ? currentSampleRate : model.sampleRate;
        const int    nFft  = AnalysisEngine::kDesignN;   // 4096
        juce::dsp::FFT fft (12);                          // 2^12 = 4096

        std::vector<float> buf (2 * nFft, 0.0f);
        const int M = (int) std::min ((int) model.l1Fir.size(), nFft);
        for (int i = 0; i < M; ++i)
            buf[i] = model.l1Fir[i];
        fft.performRealOnlyForwardTransform (buf.data());

        // Collect magnitudes.
        const int numBins = nFft / 2 + 1;
        std::vector<float> mags (numBins);
        for (int k = 0; k < numBins; ++k)
        {
            const float re = buf[2 * k], im = buf[2 * k + 1];
            mags[k] = std::sqrt (re * re + im * im);
        }

        // Normalise to 0 dB at 1 kHz so the display is centred like the
        // analysis-engine output.
        const int bin1k = juce::jlimit (1, numBins - 1,
                              (int) std::round (1000.0 * nFft / sr));
        const float normMag = (mags[bin1k] > 1e-6f) ? mags[bin1k] : 1.0f;

        for (int k = 0; k < numBins; ++k)
        {
            model.frFrequencies.push_back ((float) (k * sr / nFft));
            model.frMagnitudeDb.push_back (20.0f * std::log10 (std::max (mags[k] / normMag, 1e-6f)));
        }
    }

    // Reset FIR and dry-delay histories for all channels.
    const int numCh   = getTotalNumInputChannels();
    const int histLen = model.l1Fir.empty() ? 0 : (int) model.l1Fir.size() - 1;
    firHistory.assign (numCh, std::vector<float> (histLen, 0.0f));
    firHistoryWritePos.assign (numCh, 0);

    dryGroupDelay = model.l1Fir.empty() ? 0 : ((int) model.l1Fir.size() - 1) / 2;
    const int dlyLen = std::max (1, dryGroupDelay);
    dryDelayBuffer.assign (numCh, std::vector<float> (dlyLen, 0.0f));
    dryDelayWritePos.assign (numCh, 0);

    setLatencySamples (dryGroupDelay);

    // Rebuild overlap-save filter spectrum for the new FIR.
    // olsBlockSize is preserved from the last prepareToPlay call.
    setupOverlapSave (numCh, olsBlockSize);

    weightLpState.assign (numCh, 0.0f);
    lowShelfState.assign (numCh, {});

    // Reset multi-model blend state — rebuilt on first processBlock.
    blendedWaveshaper.clear();
    blendLoIdx = 0;
    blendHiIdx = 0;
    blendT     = 0.0f;

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
