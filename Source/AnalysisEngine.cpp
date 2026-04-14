#include "AnalysisEngine.h"

// Static member definitions
std::vector<float>                                   AnalysisEngine::wsAccum;
std::vector<int>                                     AnalysisEngine::wsCounts;
std::map<juce::String, std::vector<float>>           AnalysisEngine::perGainWsAccum;
std::map<juce::String, std::vector<int>>             AnalysisEngine::perGainWsCounts;

//==============================================================================
// Entry point
//==============================================================================
juce::String AnalysisEngine::analyseProjectFolder (const juce::File& folder,
                                                     const juce::String& deviceName,
                                                     LNLModel& model)
{
    model = LNLModel{};
    model.deviceName = deviceName;
    model.date       = juce::Time::getCurrentTime().toISO8601 (true);
    double detectedSR = 44100.0;   // updated from WAV file headers below
    bool   srDetected = false;

    // ---- 1.  Frequency response from SineSweep captures ----
    // Uses Wiener-filter estimation (equivalent to fully-converged NLMS/RLS):
    // accumulate complex cross-spectra conj(X)·Y and reference power |X|²
    // across all sweeps, then compute H = sum(conj(X)·Y) / (sum(|X|²) + λ).
    // Coherent averaging means noise cancels as N (not √N), and the Wiener
    // denominator naturally suppresses poorly-excited bins rather than
    // clamping them to a fixed noise floor.
    juce::Array<juce::File> sweepRefs;
    folder.findChildFiles (sweepRefs, juce::File::findFiles, true,
                           "*_SineSweep_ref.wav");

    const int designBins = kDesignN / 2 + 1;
    std::vector<double> crossRe  (designBins, 0.0);
    std::vector<double> crossIm  (designBins, 0.0);
    std::vector<double> refPower (designBins, 0.0);
    int sweepCount = 0;

    for (const auto& refFile : sweepRefs)
    {
        juce::File recFile (refFile.getFullPathName().replace ("_ref.wav", "_rec.wav"));
        if (! recFile.existsAsFile()) continue;

        juce::AudioBuffer<float> refBuf, recBuf;
        int numSamples;
        double sampleRate;
        if (! loadCapturePair (refFile, recFile, refBuf, recBuf, numSamples, sampleRate))
            continue;

        detectedSR = sampleRate;
        srDetected = true;

        std::vector<double> cRe, cIm, rPow;
        computeCrossSpectrum (refBuf, recBuf, numSamples, sampleRate, cRe, cIm, rPow);

        for (int k = 0; k < designBins; ++k)
        {
            crossRe  [k] += cRe  [k];
            crossIm  [k] += cIm  [k];
            refPower [k] += rPow [k];
        }
        ++sweepCount;
    }

    if (sweepCount > 0)
    {
        // Wiener estimate: H[k] = conj(X)·Y / (|X|² + λ)
        // λ is set to 1e-6 × peak reference power (~−60 dB relative).
        // Being offline we can afford a very tight floor; poorly-excited bins
        // (e.g. near Nyquist for a log sweep) are suppressed toward zero rather
        // than amplified to a noise floor as in simple spectral division.
        const double maxPow = *std::max_element (refPower.begin(), refPower.end());
        const double lambda = 1e-6 * maxPow;

        std::vector<float> hMag (designBins);
        for (int k = 0; k < designBins; ++k)
        {
            const double num = std::sqrt (crossRe[k] * crossRe[k] + crossIm[k] * crossIm[k]);
            hMag[k] = (float) (num / (refPower[k] + lambda));
        }

        smoothMagnitude (hMag, detectedSR);

        const int bin1k = juce::jlimit (1, designBins - 1,
                              (int) std::round (1000.0 * kDesignN / detectedSR));

        // Quality gate: if 1 kHz response is below -40 dB the sweep recordings
        // were likely silent or near-silent.  Fall back to identity FIR.
        if (hMag[bin1k] >= 0.01f)
        {
            // Normalise to unity gain at 1 kHz so the FIR models shape only.
            const float norm = 1.0f / hMag[bin1k];
            for (auto& v : hMag) v *= norm;

            model.l1Fir = designLinearPhaseFIR (hMag);

            // Store the tapered magnitude so the display curve matches the FIR.
            const int taperStart = (int) (0.80f * (float) (designBins - 1));
            const int taperEnd   = designBins - 1;

            for (int k = 0; k < designBins; ++k)
            {
                float v = hMag[k];
                if (k >= taperStart)
                {
                    const float t = (float) (k - taperStart) / (float) (taperEnd - taperStart);
                    v *= 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * t));
                }
                model.frFrequencies.push_back ((float) (k * detectedSR / kDesignN));
                model.frMagnitudeDb.push_back (20.0f * std::log10 (std::max (v, 1e-6f)));
            }
        }
        else
        {
            // Bad sweep data — identity FIR, flat display curve.
            model.l1Fir = { 1.0f };
            for (int k = 0; k < designBins; ++k)
            {
                model.frFrequencies.push_back ((float) (k * detectedSR / kDesignN));
                model.frMagnitudeDb.push_back (0.0f);
            }
        }
    }
    else
    {
        model.l1Fir = { 1.0f };  // identity — no sweep files found
    }

    // ---- 2.  THD + waveshaper from sine-tone captures ----
    wsAccum .assign (kTableSize, 0.0f);
    wsCounts.assign (kTableSize, 0);
    perGainWsAccum .clear();
    perGainWsCounts.clear();

    for (const auto& [stimName, fundHz] :
         std::initializer_list<std::pair<juce::String, float>>
         { { "Sine1kHz", 1000.0f }, { "Sine100Hz", 100.0f } })
    {
        juce::Array<juce::File> toneRefs;
        folder.findChildFiles (toneRefs, juce::File::findFiles, true,
                               "*_" + stimName + "_ref.wav");

        for (const auto& refFile : toneRefs)
        {
            juce::File recFile (refFile.getFullPathName().replace ("_ref.wav", "_rec.wav"));
            if (! recFile.existsAsFile()) continue;

            juce::AudioBuffer<float> refBuf, recBuf;
            int numSamples;
            double sampleRate;
            if (! loadCapturePair (refFile, recFile, refBuf, recBuf, numSamples, sampleRate))
                continue;

            if (! srDetected) { detectedSR = sampleRate; srDetected = true; }

            // Gain label is the name of the parent folder (e.g. "75%").
            const juce::String gainLabel = refFile.getParentDirectory().getFileName();

            analyseSineTone (refBuf, recBuf, numSamples, sampleRate,
                             fundHz, gainLabel, stimName, model);
        }
    }

    finaliseWaveshaper (model);
    populateGainModels (model);
    model.sampleRate = detectedSR;

    return {};
}

//==============================================================================
// Wiener-filter transfer function estimation (offline, accuracy-first).
//
// Returns conj(X)·Y (complex cross-spectrum) and |X|² (reference power) both
// resampled to designBins resolution.  The caller accumulates these across
// multiple sweeps and divides once at the end:
//
//   H[k] = sum_i( conj(Xi)·Yi ) / ( sum_i(|Xi|²) + λ )
//
// This is the frequency-domain Wiener filter — equivalent to running NLMS with
// a step size that approaches zero (fully converged, minimum-MSE solution).
// Coherent averaging improves SNR as N (not √N), and the λ denominator
// suppresses bins where the reference has low energy rather than amplifying them.
//==============================================================================
void AnalysisEngine::computeCrossSpectrum (
    const juce::AudioBuffer<float>& ref,
    const juce::AudioBuffer<float>& rec,
    int numSamples,
    double /*sampleRate*/,
    std::vector<double>& outCrossRe,
    std::vector<double>& outCrossIm,
    std::vector<double>& outRefPower)
{
    // Choose FFT order to cover numSamples.
    int fftOrder = 1;
    while ((1 << fftOrder) < numSamples) ++fftOrder;
    const int N = 1 << fftOrder;

    juce::dsp::FFT fft (fftOrder);

    std::vector<float> refData (2 * N, 0.0f);
    std::vector<float> recData (2 * N, 0.0f);

    const int numSrc = std::min (numSamples, N);
    std::copy_n (ref.getReadPointer (0), numSrc, refData.data());
    std::copy_n (rec.getReadPointer (0), numSrc, recData.data());

    fft.performRealOnlyForwardTransform (refData.data());
    fft.performRealOnlyForwardTransform (recData.data());

    const int nativeBins = N / 2 + 1;

    // Compute complex cross-spectrum conj(X)·Y and reference power |X|²
    // at native FFT bins using double precision to preserve accuracy
    // across the wide dynamic range of a log sine sweep.
    std::vector<double> nativeCrossRe (nativeBins);
    std::vector<double> nativeCrossIm (nativeBins);
    std::vector<double> nativeRefPow  (nativeBins);

    for (int k = 0; k < nativeBins; ++k)
    {
        const double Xre = refData[2 * k],  Xim = refData[2 * k + 1];
        const double Yre = recData[2 * k],  Yim = recData[2 * k + 1];
        // conj(X) · Y = (Xre − j·Xim)(Yre + j·Yim)
        nativeCrossRe[k] = Xre * Yre + Xim * Yim;
        nativeCrossIm[k] = Xre * Yim - Xim * Yre;
        nativeRefPow [k] = Xre * Xre + Xim * Xim;
    }

    // Resample to designBins via linear interpolation.
    const int designBins = kDesignN / 2 + 1;
    outCrossRe .assign (designBins, 0.0);
    outCrossIm .assign (designBins, 0.0);
    outRefPower.assign (designBins, 0.0);

    for (int i = 0; i < designBins; ++i)
    {
        const double frac = (double) i / (double) (designBins - 1) * (double) (nativeBins - 1);
        const int    j0   = (int) frac;
        const int    j1   = std::min (j0 + 1, nativeBins - 1);
        const double t    = frac - (double) j0;
        outCrossRe [i] = nativeCrossRe[j0] * (1.0 - t) + nativeCrossRe[j1] * t;
        outCrossIm [i] = nativeCrossIm[j0] * (1.0 - t) + nativeCrossIm[j1] * t;
        outRefPower[i] = nativeRefPow [j0] * (1.0 - t) + nativeRefPow [j1] * t;
    }
}

//==============================================================================
// 1/3-octave band smoothing of a half-spectrum magnitude array.
//==============================================================================
void AnalysisEngine::smoothMagnitude (std::vector<float>& hMag, double sampleRate)
{
    const int numBins = (int) hMag.size();
    std::vector<float> smoothed (numBins, 0.0f);

    // DC bin: the sine sweep has no DC energy so hMag[0] is the fallback value
    // (1.0 before normalisation).  Using that hard-coded value creates a
    // discontinuity in the spectrum when the transformer rolls off below ~30 Hz —
    // the FIR then sees a step from 1.0 at DC to the measured roll-off value at
    // the first measured bin, causing extra ringing that pushes the effective
    // high-pass point higher than the transformer actually warrants.
    // Fix: linearly extrapolate from the first two measured bins back to DC.
    if (numBins > 2)
    {
        const float slope = hMag[2] - hMag[1];
        smoothed[0] = std::max (0.01f, hMag[1] - slope);
    }
    else
    {
        smoothed[0] = (numBins > 1) ? hMag[1] : hMag[0];
    }

    const float halfWidth = std::pow (2.0f, 1.0f / 6.0f);  // half an octave third

    for (int k = 1; k < numBins; ++k)
    {
        const float freq   = (float) (k * sampleRate / kDesignN);
        const float freqLo = freq / halfWidth;
        const float freqHi = freq * halfWidth;

        const int binLo = std::max (1, (int) (freqLo * kDesignN / sampleRate));
        const int binHi = std::min (numBins - 1, (int) (freqHi * kDesignN / sampleRate));

        float sum = 0.0f;
        int   cnt = 0;
        for (int j = binLo; j <= binHi; ++j) { sum += hMag[j]; ++cnt; }
        smoothed[k] = (cnt > 0) ? sum / (float) cnt : hMag[k];
    }

    hMag = smoothed;
}

//==============================================================================
// Rebuild the L1 FIR at a different sample rate using stored FR display data.
// frFreqHz and frMagDb are the downsampled display arrays saved in LNLModel.
//==============================================================================
std::vector<float> AnalysisEngine::rebuildFirAtSampleRate (
    const std::vector<float>& frFreqHz,
    const std::vector<float>& frMagDb,
    double targetSampleRate,
    int numTaps)
{
    if (frFreqHz.empty() || frFreqHz.size() != frMagDb.size())
        return { 1.0f };  // identity — no stored FR data

    const int numStored = (int) frFreqHz.size();

    // Convert stored dB values back to linear magnitude.
    std::vector<float> storedLinear (numStored);
    for (int i = 0; i < numStored; ++i)
        storedLinear[i] = std::pow (10.0f, frMagDb[i] / 20.0f);

    // Build a new design-grid magnitude array at targetSampleRate.
    const int designBins = kDesignN / 2 + 1;
    std::vector<float> hMag (designBins);

    for (int k = 0; k < designBins; ++k)
    {
        const float freq = (float) (k * targetSampleRate / kDesignN);

        if (freq <= frFreqHz[0])
        {
            hMag[k] = storedLinear[0];
        }
        else if (freq >= frFreqHz[numStored - 1])
        {
            // Beyond the measured range — hold the last value (typically Nyquist
            // of the capture rate, which may be above the plugin's Nyquist).
            hMag[k] = storedLinear[numStored - 1];
        }
        else
        {
            // Binary search for the enclosing interval.
            int lo = 0, hi = numStored - 1;
            while (hi - lo > 1)
            {
                const int mid = (lo + hi) / 2;
                if (frFreqHz[mid] <= freq) lo = mid; else hi = mid;
            }
            const float t = (frFreqHz[hi] > frFreqHz[lo])
                            ? (freq - frFreqHz[lo]) / (frFreqHz[hi] - frFreqHz[lo])
                            : 0.0f;
            hMag[k] = storedLinear[lo] * (1.0f - t) + storedLinear[hi] * t;
        }
    }

    return designLinearPhaseFIR (hMag, numTaps);
}

//==============================================================================
// Design a linear-phase FIR from a half-spectrum magnitude (kDesignN/2+1 bins).
//==============================================================================
std::vector<float> AnalysisEngine::designLinearPhaseFIR (const std::vector<float>& hMag,
                                                           int numTaps)
{
    // Build a full symmetric zero-phase spectrum in interleaved [re, im] format,
    // then IFFT to get the zero-phase impulse response.
    const int N = kDesignN;
    juce::dsp::FFT fft (12);  // 2^12 = 4096

    // Apply a half-cosine anti-aliasing taper from 85% of Nyquist to Nyquist.
    // This suppresses near-Nyquist measurement noise (low SNR from recording chain
    // roll-off) that would otherwise appear as a boost at 18–20 kHz on the display
    // and in the FIR output.  The taper is relative to the design grid so it
    // always maps to 85–100% of the actual Nyquist regardless of sample rate.
    const int designBins = N / 2 + 1;
    const int taperStart = (int) (0.80f * (float) (designBins - 1));
    const int taperEnd   = designBins - 1;

    std::vector<float> hTapered (hMag);
    for (int k = taperStart; k <= taperEnd; ++k)
    {
        const float t = (float) (k - taperStart) / (float) (taperEnd - taperStart);
        const float taper = 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * t));
        hTapered[k] *= taper;
    }

    std::vector<float> spectrum (2 * N, 0.0f);

    // DC
    spectrum[0] = hTapered[0];  spectrum[1] = 0.0f;

    // Positive and mirrored negative frequencies (imaginary = 0 → zero phase).
    for (int k = 1; k < N / 2; ++k)
    {
        spectrum[2 * k]           = hTapered[k];  spectrum[2 * k + 1]           = 0.0f;
        spectrum[2 * (N - k)]     = hTapered[k];  spectrum[2 * (N - k) + 1]     = 0.0f;
    }

    // Nyquist
    spectrum[N] = hTapered[N / 2];  spectrum[N + 1] = 0.0f;

    fft.performRealOnlyInverseTransform (spectrum.data());

    // spectrum[0..N-1] is the zero-phase impulse response (peak at index 0,
    // tails in both directions circularly).  Extract numTaps centred on index 0.
    // Note: performRealOnlyInverseTransform already normalises by 1/N internally,
    // so no additional scale factor is needed here.
    const int   half   = numTaps / 2;
    std::vector<float> firCoeffs (numTaps, 0.0f);

    for (int i = 0; i < numTaps; ++i)
    {
        const int n = ((i - half) % N + N) % N;
        const float hann = 0.5f * (1.0f - std::cos (
            juce::MathConstants<float>::twoPi * (float) i / (float) (numTaps - 1)));
        firCoeffs[i] = spectrum[n] * hann;
    }

    return firCoeffs;
}

//==============================================================================
// THD computation and waveshaper scatter accumulation for one sine-tone capture.
//==============================================================================
void AnalysisEngine::analyseSineTone (const juce::AudioBuffer<float>& ref,
                                       const juce::AudioBuffer<float>& rec,
                                       int numSamples,
                                       double sampleRate,
                                       float fundamentalHz,
                                       const juce::String& gainLabel,
                                       const juce::String& stimulusName,
                                       LNLModel& model)
{
    if (numSamples < 4096) return;

    // Use 65536 samples from the middle of the capture (avoids transients).
    constexpr int fftSize = 65536;
    const int fftOrder = 16;
    const int startSample = std::max (0, numSamples / 2 - fftSize / 2);
    const int available   = std::min (fftSize, numSamples - startSample);

    juce::dsp::FFT fft (fftOrder);

    std::vector<float> recData (2 * fftSize, 0.0f);
    const float* recPtr = rec.getReadPointer (0);
    std::copy_n (recPtr + startSample, available, recData.data());

    // Hann window to reduce spectral leakage.
    for (int n = 0; n < available; ++n)
        recData[n] *= 0.5f * (1.0f - std::cos (
            juce::MathConstants<float>::twoPi * (float) n / (float) (available - 1)));

    fft.performRealOnlyForwardTransform (recData.data());

    // Locate fundamental bin peak within ±10 bins of expected.
    const int fundBin = (int) std::round (fundamentalHz * fftSize / sampleRate);
    int   peakBin = fundBin;
    float peakMag = 0.0f;

    for (int k = std::max (1, fundBin - 10); k <= std::min (fftSize / 2, fundBin + 10); ++k)
    {
        const float re = recData[2 * k], im = recData[2 * k + 1];
        const float mag = std::sqrt (re * re + im * im);
        if (mag > peakMag) { peakMag = mag;  peakBin = k; }
    }

    // Sum power in harmonics 2–9.
    float harmonicPower = 0.0f;
    for (int h = 2; h <= 9; ++h)
    {
        const int hBin = peakBin * h;
        if (hBin >= fftSize / 2) break;

        float hMag = 0.0f;
        for (int k = std::max (1, hBin - 3); k <= std::min (fftSize / 2, hBin + 3); ++k)
        {
            const float re = recData[2 * k], im = recData[2 * k + 1];
            hMag = std::max (hMag, std::sqrt (re * re + im * im));
        }
        harmonicPower += hMag * hMag;
    }

    THDEntry entry;
    entry.gainLabel    = gainLabel;
    entry.stimulusName = stimulusName;
    const float fundPower = peakMag * peakMag;
    entry.thdPercent      = (fundPower > 0.0f)
                                ? 100.0f * std::sqrt (harmonicPower / fundPower)
                                : 0.0f;
    entry.fundamentalAmplitudeDb = 20.0f * std::log10 (
                                       std::max (peakMag / (float) (fftSize / 2), 1e-6f));
    model.thdResults.push_back (entry);

    // Remove DC offset before scatter accumulation.
    // Asymmetric clipping devices (e.g. diode mixtures) can bias the output;
    // leaving DC in would shift the entire waveshaper table and add a constant
    // offset to every processed sample in the plugin.
    double dcSum = 0.0;
    for (int n = startSample; n < startSample + available; ++n)
        dcSum += recPtr[n];
    const float dcOffset = (float) (dcSum / available);

    // Accumulate waveshaper scatter: bin ref[n] → average rec[n] (DC-removed).
    const float* refPtr = ref.getReadPointer (0);

    // Ensure per-gain buffers exist for this gain label.
    if (perGainWsAccum.find (gainLabel) == perGainWsAccum.end())
    {
        perGainWsAccum [gainLabel].assign (kTableSize, 0.0f);
        perGainWsCounts[gainLabel].assign (kTableSize, 0);
    }
    auto& pgAccum  = perGainWsAccum [gainLabel];
    auto& pgCounts = perGainWsCounts[gainLabel];

    for (int n = startSample; n < startSample + available; ++n)
    {
        const float x = juce::jlimit (-1.0f, 1.0f, refPtr[n]);
        const int binIdx = juce::jlimit (0, kTableSize - 1,
                               (int) ((x + 1.0f) * 0.5f * (float) (kTableSize - 1)));
        const float y = recPtr[n] - dcOffset;
        wsAccum[binIdx]  += y;
        wsCounts[binIdx] += 1;
        pgAccum [binIdx] += y;
        pgCounts[binIdx] += 1;
    }
}

//==============================================================================
// Shared waveshaper finalisation: average, interpolate, extrapolate, normalise.
//==============================================================================
void AnalysisEngine::finaliseWaveshaperInto (std::vector<float>& wsOut,
                                              const std::vector<float>& accum,
                                              const std::vector<int>&   counts)
{
    wsOut.resize (kTableSize);

    // Average each populated bin.
    for (int i = 0; i < kTableSize; ++i)
    {
        if (counts[i] > 0)
            wsOut[i] = accum[i] / (float) counts[i];
        else
            wsOut[i] = -1.0f + 2.0f * (float) i / (float) (kTableSize - 1);  // identity
    }

    // Fill any un-hit bins by linear interpolation between neighbours.
    for (int i = 1; i < kTableSize - 1; ++i)
    {
        if (counts[i] == 0)
        {
            int j = i + 1;
            while (j < kTableSize - 1 && counts[j] == 0) ++j;

            if (counts[i - 1] > 0 && counts[j] > 0)
            {
                const float y0   = wsOut[i - 1];
                const float y1   = wsOut[j];
                const int   span = j - (i - 1);
                for (int k = i; k < j; ++k)
                {
                    const float t = (float) (k - (i - 1)) / (float) span;
                    wsOut[k] = y0 * (1.0f - t) + y1 * t;
                }
                i = j - 1;
            }
        }
    }

    // Extrapolate linearly at extreme unpopulated bins.
    {
        int firstPop = -1, lastPop = -1;
        for (int i = 0; i < kTableSize; ++i)
            if (counts[i] > 0) { if (firstPop < 0) firstPop = i; lastPop = i; }

        if (firstPop > 0)
        {
            float slope = -1.0f;
            for (int i = firstPop + 1; i <= std::min (firstPop + 8, kTableSize - 1); ++i)
            {
                if (counts[i] > 0)
                {
                    slope = (wsOut[i] - wsOut[firstPop]) / (float) (i - firstPop);
                    break;
                }
            }
            for (int i = 0; i < firstPop; ++i)
                wsOut[i] = wsOut[firstPop] + slope * (float) (i - firstPop);
        }

        if (lastPop >= 0 && lastPop < kTableSize - 1)
        {
            float slope = -1.0f;
            for (int i = lastPop - 1; i >= std::max (lastPop - 8, 0); --i)
            {
                if (counts[i] > 0)
                {
                    slope = (wsOut[lastPop] - wsOut[i]) / (float) (lastPop - i);
                    break;
                }
            }
            for (int i = lastPop + 1; i < kTableSize; ++i)
                wsOut[i] = wsOut[lastPop] + slope * (float) (i - lastPop);
        }
    }

    // Normalise small-signal slope to unity.
    const int   center    = kTableSize / 2;
    const float stepSize  = 2.0f / (float) (kTableSize - 1);
    const int   window    = 8;
    float       slopeSum  = 0.0f;
    int         slopeCnt  = 0;
    for (int d = 1; d <= window; ++d)
    {
        if (center - d >= 0 && center + d < kTableSize)
        {
            slopeSum += (wsOut[center + d] - wsOut[center - d])
                        / (2.0f * (float) d * stepSize);
            ++slopeCnt;
        }
    }
    if (slopeCnt > 0)
    {
        const float slope = slopeSum / (float) slopeCnt;
        if (std::abs (slope) > 1e-6f)
            for (auto& v : wsOut) v /= slope;
    }
}

//==============================================================================
// Finalise waveshaper table from accumulated scatter data.
//==============================================================================
void AnalysisEngine::finaliseWaveshaper (LNLModel& model)
{
    finaliseWaveshaperInto (model.waveshaper, wsAccum, wsCounts);
}

//==============================================================================
// Build model.gainModels from per-gain-label accumulators.
// Each gain label becomes one GainModel with a separately finalised waveshaper.
// Models are sorted by the numeric value parsed from the gain label
// (e.g. "25%" → 25, "75%" → 75) and gainValue is normalised 0…1.
//==============================================================================
void AnalysisEngine::populateGainModels (LNLModel& model)
{
    if (perGainWsAccum.empty())
        return;

    // Collect labels and their parsed numeric values for sorting.
    std::vector<std::pair<float, juce::String>> sortable;
    for (const auto& [label, _] : perGainWsAccum)
    {
        const float v = label.trimCharactersAtEnd ("%").getFloatValue();
        sortable.emplace_back (v, label);
    }
    std::sort (sortable.begin(), sortable.end(),
               [] (const auto& a, const auto& b) { return a.first < b.first; });

    const int N = (int) sortable.size();
    model.gainModels.clear();
    model.gainModels.reserve (N);

    for (int idx = 0; idx < N; ++idx)
    {
        const juce::String& label  = sortable[idx].second;
        GainModel gm;
        gm.gainLabel  = label;
        gm.gainValue  = (N > 1) ? (float) idx / (float) (N - 1) : 0.5f;
        finaliseWaveshaperInto (gm.waveshaper,
                                perGainWsAccum .at (label),
                                perGainWsCounts.at (label));
        model.gainModels.push_back (std::move (gm));
    }
}

//==============================================================================
// Load a ref/rec WAV pair from disk.
//==============================================================================
bool AnalysisEngine::loadCapturePair (const juce::File& refFile,
                                       const juce::File& recFile,
                                       juce::AudioBuffer<float>& refBuf,
                                       juce::AudioBuffer<float>& recBuf,
                                       int& numSamples,
                                       double& sampleRate)
{
    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> refReader (fmt.createReaderFor (refFile));
    std::unique_ptr<juce::AudioFormatReader> recReader (fmt.createReaderFor (recFile));

    if (refReader == nullptr || recReader == nullptr)
        return false;

    sampleRate = refReader->sampleRate;
    numSamples = (int) std::min (refReader->lengthInSamples, recReader->lengthInSamples);

    refBuf.setSize ((int) refReader->numChannels, numSamples);
    recBuf.setSize ((int) recReader->numChannels, numSamples);

    refReader->read (&refBuf, 0, numSamples, 0, true, true);
    recReader->read (&recBuf, 0, numSamples, 0, true, true);

    return true;
}
