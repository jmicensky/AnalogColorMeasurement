// Offline analysis pipeline that builds the L-N-L gray-box model from captured ref/rec WAV pairs.
// Estimates the L1 FIR via Wiener-filter averaging of sine sweeps, and fits per-gain-level waveshaper tables from sine-tone scatter for multi-model interpolation.

#include "AnalysisEngine.h"

// Static member definitions
std::vector<float>                                   AnalysisEngine::wsAccum;
std::vector<int>                                     AnalysisEngine::wsCounts;
std::map<juce::String, std::vector<float>>           AnalysisEngine::perGainWsAccum;
std::map<juce::String, std::vector<int>>             AnalysisEngine::perGainWsCounts;
std::vector<float>                                   AnalysisEngine::wsAccumR;
std::vector<int>                                     AnalysisEngine::wsCountsR;
std::map<juce::String, std::vector<float>>           AnalysisEngine::perGainWsAccumR;
std::map<juce::String, std::vector<int>>             AnalysisEngine::perGainWsCountsR;

//==============================================================================
// Entry point
//==============================================================================
juce::String AnalysisEngine::analyseProjectFolder (const juce::File& folder,
                                                     const juce::String& deviceName,
                                                     LNLModel& model)
{
    // Preserve caller-supplied fields before the full reset.
    const float callerInputPadDb = model.inputPadDb;

    model = LNLModel{};
    model.deviceName  = deviceName;
    model.date        = juce::Time::getCurrentTime().toISO8601 (true);
    model.inputPadDb  = callerInputPadDb;
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

    // ---- 1b. Volterra kernel identification from sweep captures ----
    // Fits h1 (linear, M1 taps) and h2 (2nd-order, M2 taps) via normal
    // equations solved with Cholesky.  Uses the same sweep captures already
    // loaded for the Wiener FR estimate — no extra recording required.
    // On success modelType is set to Volterra and processBlock uses the
    // Volterra path (h1 replaces l1Fir at runtime).  Falls back silently to
    // LNL if the data is insufficient or the matrix is ill-conditioned.
    if (sweepCount > 0)
    {
        for (const auto& refFile : sweepRefs)
        {
            juce::File recFile (refFile.getFullPathName().replace ("_ref.wav", "_rec.wav"));
            if (! recFile.existsAsFile()) continue;

            juce::AudioBuffer<float> vRefBuf, vRecBuf;
            int vSamples;  double vSR;
            if (! loadCapturePair (refFile, recFile, vRefBuf, vRecBuf, vSamples, vSR))
                continue;

            // Default depths: M1=32 taps (~0.7 ms linear memory),
            // M2=8 taps (~0.18 ms quadratic memory, 36 interaction terms).
            // P = 32 + 36 = 68 unknowns — Cholesky on a 68×68 matrix is fast.
            if (identifyVolterraKernels (vRefBuf, vRecBuf, vSamples, model, vSR).isEmpty())
                break;   // success — first good sweep provides enough data
        }
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

    finaliseWaveshaper (model, 0);
    populateGainModels (model, 0);
    populatePerLevelVolterraKernels (model, folder, 0);
    model.sampleRate = detectedSR;

    // ---- R-channel pass (stereo devices only) ----
    // Reload the same captures and repeat the analysis for channel 1.
    // Any mono file silently clamps to its only channel so mono captures are unaffected.
    {
        // Check whether any sweep or tone file has a right channel.
        bool hasStereoFiles = false;
        {
            juce::Array<juce::File> probeFiles;
            folder.findChildFiles (probeFiles, juce::File::findFiles, true, "*_ref.wav");
            for (const auto& f : probeFiles)
            {
                juce::AudioFormatManager fmt;
                fmt.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> rdr (fmt.createReaderFor (f));
                if (rdr && rdr->numChannels >= 2) { hasStereoFiles = true; break; }
            }
        }

        if (hasStereoFiles)
        {
            // ---- 1R. Frequency response (R channel) ----
            std::vector<double> crossReR  (designBins, 0.0);
            std::vector<double> crossImR  (designBins, 0.0);
            std::vector<double> refPowerR (designBins, 0.0);
            int sweepCountR = 0;

            for (const auto& refFile : sweepRefs)
            {
                juce::File recFile (refFile.getFullPathName().replace ("_ref.wav", "_rec.wav"));
                if (! recFile.existsAsFile()) continue;

                juce::AudioBuffer<float> refBuf, recBuf;
                int numSamples;  double sampleRate;
                if (! loadCapturePair (refFile, recFile, refBuf, recBuf, numSamples, sampleRate))
                    continue;

                std::vector<double> cRe, cIm, rPow;
                computeCrossSpectrum (refBuf, recBuf, numSamples, sampleRate,
                                      cRe, cIm, rPow, 1);
                for (int k = 0; k < designBins; ++k)
                {
                    crossReR  [k] += cRe  [k];
                    crossImR  [k] += cIm  [k];
                    refPowerR [k] += rPow [k];
                }
                ++sweepCountR;
            }

            if (sweepCountR > 0)
            {
                const double maxPow = *std::max_element (refPowerR.begin(), refPowerR.end());
                const double lambda = 1e-6 * maxPow;

                std::vector<float> hMagR (designBins);
                for (int k = 0; k < designBins; ++k)
                {
                    const double num = std::sqrt (crossReR[k] * crossReR[k]
                                                  + crossImR[k] * crossImR[k]);
                    hMagR[k] = (float) (num / (refPowerR[k] + lambda));
                }

                smoothMagnitude (hMagR, detectedSR);

                const int bin1k = juce::jlimit (1, designBins - 1,
                                      (int) std::round (1000.0 * kDesignN / detectedSR));

                if (hMagR[bin1k] >= 0.01f)
                {
                    const float norm = 1.0f / hMagR[bin1k];
                    for (auto& v : hMagR) v *= norm;

                    model.l1FirR = designLinearPhaseFIR (hMagR);

                    const int taperStart = (int) (0.80f * (float) (designBins - 1));
                    const int taperEnd   = designBins - 1;
                    for (int k = 0; k < designBins; ++k)
                    {
                        float v = hMagR[k];
                        if (k >= taperStart)
                        {
                            const float t = (float) (k - taperStart)
                                            / (float) (taperEnd - taperStart);
                            v *= 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * t));
                        }
                        model.frMagnitudeDbR.push_back (20.0f * std::log10 (std::max (v, 1e-6f)));
                    }
                }
                else
                {
                    model.l1FirR = { 1.0f };
                    model.frMagnitudeDbR.assign (designBins, 0.0f);
                }

                // ---- 1bR. Volterra R channel (only for Volterra artifacts) ----
                if (model.modelType == LNLModel::ModelType::Volterra)
                for (const auto& refFile : sweepRefs)
                {
                    juce::File recFile (refFile.getFullPathName().replace ("_ref.wav", "_rec.wav"));
                    if (! recFile.existsAsFile()) continue;

                    juce::AudioBuffer<float> vRefBuf, vRecBuf;
                    int vSamples;  double vSR;
                    if (! loadCapturePair (refFile, recFile, vRefBuf, vRecBuf, vSamples, vSR))
                        continue;

                    if (identifyVolterraKernels (vRefBuf, vRecBuf, vSamples, model, vSR,
                                                 32, 8, 1).isEmpty())
                        break;
                }
            }

            // ---- 2R. Waveshaper from sine tones (R channel) ----
            wsAccumR .assign (kTableSize, 0.0f);
            wsCountsR.assign (kTableSize, 0);
            perGainWsAccumR .clear();
            perGainWsCountsR.clear();

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
                    int numSamples;  double sampleRate;
                    if (! loadCapturePair (refFile, recFile, refBuf, recBuf, numSamples, sampleRate))
                        continue;

                    const juce::String gainLabel = refFile.getParentDirectory().getFileName();
                    analyseSineTone (refBuf, recBuf, numSamples, sampleRate,
                                     fundHz, gainLabel, stimName, model, 1);
                }
            }

            finaliseWaveshaper (model, 1);
            populateGainModels (model, 1);
            populatePerLevelVolterraKernels (model, folder, 1);
        }
    }

    return {};
}

//==============================================================================
// Per-gain-level Volterra kernel identification.
// For each GainModel whose folder contains a sweep capture, identifies h1/h2
// using a scratch LNLModel so the global kernels on `model` are not touched.
//==============================================================================
void AnalysisEngine::populatePerLevelVolterraKernels (LNLModel& model,
                                                       const juce::File& folder,
                                                       int channel)
{
    if (model.modelType != LNLModel::ModelType::Volterra)
        return;

    auto& dest = (channel == 0) ? model.gainModels : model.gainModelsR;

    for (auto& gm : dest)
    {
        const juce::File levelFolder = folder.getChildFile (gm.gainLabel);
        if (! levelFolder.isDirectory()) continue;

        // Find sweep ref files in this gain level's subfolder (skip tone files).
        juce::Array<juce::File> candidates;
        levelFolder.findChildFiles (candidates, juce::File::findFiles, false, "*_ref.wav");

        for (const auto& refFile : candidates)
        {
            const juce::String name = refFile.getFileName();
            if (name.contains ("Sine1kHz") || name.contains ("Sine100Hz"))
                continue;

            juce::File recFile (refFile.getFullPathName().replace ("_ref.wav", "_rec.wav"));
            if (! recFile.existsAsFile()) continue;

            juce::AudioBuffer<float> refBuf, recBuf;
            int numSamples;  double sampleRate;
            if (! loadCapturePair (refFile, recFile, refBuf, recBuf, numSamples, sampleRate))
                continue;

            // Use a scratch model so the global h1/h2 on `model` are not overwritten.
            LNLModel scratch;
            scratch.modelType  = LNLModel::ModelType::Volterra;
            scratch.volterraM1 = model.volterraM1;
            scratch.volterraM2 = model.volterraM2;

            const juce::String err = identifyVolterraKernels (
                refBuf, recBuf, numSamples, scratch, sampleRate,
                model.volterraM1, model.volterraM2, 0);

            if (err.isEmpty())
            {
                if (channel == 0)
                {
                    gm.volterraH1 = std::move (scratch.volterraH1);
                    gm.volterraH2 = std::move (scratch.volterraH2);
                }
                else
                {
                    gm.volterraH1R = std::move (scratch.volterraH1);
                    gm.volterraH2R = std::move (scratch.volterraH2);
                }
                break;   // one sweep per gain level is sufficient
            }
        }
    }
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
    std::vector<double>& outRefPower,
    int channel)
{
    const int refCh = juce::jlimit (0, ref.getNumChannels() - 1, channel);
    const int recCh = juce::jlimit (0, rec.getNumChannels() - 1, channel);

    // Choose FFT order to cover numSamples.
    int fftOrder = 1;
    while ((1 << fftOrder) < numSamples) ++fftOrder;
    const int N = 1 << fftOrder;

    juce::dsp::FFT fft (fftOrder);

    std::vector<float> refData (2 * N, 0.0f);
    std::vector<float> recData (2 * N, 0.0f);

    const int numSrc = std::min (numSamples, N);
    std::copy_n (ref.getReadPointer (refCh), numSrc, refData.data());
    std::copy_n (rec.getReadPointer (recCh), numSrc, recData.data());

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

    // Use minimum-phase design so the FIR has no pre-ringing.
    // Analog hardware is inherently minimum-phase; a linear-phase FIR would
    // smear transients backwards in time, causing audible artefacts on percussive
    // material.  The minimum-phase FIR also reports near-zero latency to the host.
    return designMinimumPhaseFIR (hMag, numTaps);
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
// Design a minimum-phase FIR from a half-spectrum magnitude (kDesignN/2+1 bins).
// Cepstral method (Oppenheim & Schafer, "Discrete-Time Signal Processing" §12):
//   1. log|H(ω)| → conjugate-symmetric spectrum → IFFT → real cepstrum c[n]
//   2. Causal liftering: keep c[0], double c[1..N/2-1], keep c[N/2], zero c[N/2+1..]
//   3. FFT → complex log-spectrum C[k]
//   4. exp(C[k]) → minimum-phase spectrum H_min[k]
//   5. IFFT → minimum-phase IR h_min[n]  (truncate to numTaps with tail fade)
// Energy is concentrated at n=0 → no pre-ringing, group delay ≈ 0.
//==============================================================================
std::vector<float> AnalysisEngine::designMinimumPhaseFIR (const std::vector<float>& hMag,
                                                           int numTaps)
{
    const int N = kDesignN;
    juce::dsp::FFT fft (12);   // 2^12 = 4096

    const int designBins = N / 2 + 1;

    // Apply the same high-frequency taper as designLinearPhaseFIR.
    const int taperStart = (int) (0.80f * (float) (designBins - 1));
    const int taperEnd   = designBins - 1;
    std::vector<float> hTapered (hMag);
    for (int k = taperStart; k <= taperEnd; ++k)
    {
        const float t = (float) (k - taperStart) / (float) (taperEnd - taperStart);
        hTapered[k] *= 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * t));
    }

    // --- Step 1: Build conjugate-symmetric log-magnitude spectrum (zero phase). ---
    std::vector<float> spectrum (2 * N, 0.0f);
    spectrum[0] = std::log (std::max (hTapered[0], 1e-6f));  spectrum[1] = 0.0f;
    for (int k = 1; k < N / 2; ++k)
    {
        const float lm = std::log (std::max (hTapered[k], 1e-6f));
        spectrum[2 * k]           = lm;  spectrum[2 * k + 1]           = 0.0f;
        spectrum[2 * (N - k)]     = lm;  spectrum[2 * (N - k) + 1]     = 0.0f;
    }
    spectrum[N] = std::log (std::max (hTapered[N / 2], 1e-6f));  spectrum[N + 1] = 0.0f;

    // --- Step 2: IFFT → real cepstrum c[n] in spectrum[0..N-1]. ---
    fft.performRealOnlyInverseTransform (spectrum.data());

    // --- Step 3: Causal liftering. ---
    // n=0: unchanged (DC of log-mag), n=1..N/2-1: doubled (fold negative-time),
    // n=N/2: unchanged (Nyquist), n=N/2+1..N-1: zeroed (remove negative-time).
    for (int n = 1; n < N / 2; ++n)
        spectrum[n] *= 2.0f;
    for (int n = N / 2 + 1; n < N; ++n)
        spectrum[n] = 0.0f;

    // --- Step 4: FFT of windowed (real) cepstrum → complex log-spectrum C[k]. ---
    // Output is conjugate-symmetric because the input is real.
    fft.performRealOnlyForwardTransform (spectrum.data());

    // --- Step 5: exp(C[k]) → minimum-phase complex spectrum H_min[k]. ---
    for (int k = 0; k < N; ++k)
    {
        const float logMag = spectrum[2 * k];
        const float phase  = spectrum[2 * k + 1];
        const float mag    = std::exp (logMag);
        spectrum[2 * k]     = mag * std::cos (phase);
        spectrum[2 * k + 1] = mag * std::sin (phase);
    }

    // --- Step 6: IFFT → minimum-phase IR h_min[n] in spectrum[0..N-1]. ---
    fft.performRealOnlyInverseTransform (spectrum.data());

    // --- Step 7: Truncate to numTaps with a cosine fade-out on the last quarter. ---
    // Unlike linear-phase, NO symmetric window is applied at the start — the energy
    // is naturally front-loaded, so windowing the start would add artificial pre-ring.
    std::vector<float> firCoeffs (numTaps, 0.0f);
    const int copyLen   = std::min (numTaps, N);
    const int fadeLen   = std::max (1, numTaps / 4);
    const int fadeStart = numTaps - fadeLen;

    for (int i = 0; i < copyLen; ++i)
    {
        float win = 1.0f;
        if (i >= fadeStart)
        {
            const float t = (float) (i - fadeStart) / (float) (fadeLen - 1);
            win = 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * t));
        }
        firCoeffs[i] = spectrum[i] * win;
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
                                       LNLModel& model,
                                       int channel)
{
    if (numSamples < 4096) return;

    // Use 65536 samples from the middle of the capture (avoids transients).
    constexpr int fftSize = 65536;
    const int fftOrder = 16;
    const int startSample = std::max (0, numSamples / 2 - fftSize / 2);
    const int available   = std::min (fftSize, numSamples - startSample);

    juce::dsp::FFT fft (fftOrder);

    const int recCh = juce::jlimit (0, rec.getNumChannels() - 1, channel);
    const int refCh = juce::jlimit (0, ref.getNumChannels() - 1, channel);

    std::vector<float> recData (2 * fftSize, 0.0f);
    const float* recPtr = rec.getReadPointer (recCh);
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
    const float* refPtr = ref.getReadPointer (refCh);

    // Select the right accumulator set based on channel.
    auto& wsAcc  = (channel == 0) ? wsAccum  : wsAccumR;
    auto& wsCnts = (channel == 0) ? wsCounts : wsCountsR;
    auto& pgMap  = (channel == 0) ? perGainWsAccum  : perGainWsAccumR;
    auto& pgCMap = (channel == 0) ? perGainWsCounts : perGainWsCountsR;

    // Ensure per-gain buffers exist for this gain label.
    if (pgMap.find (gainLabel) == pgMap.end())
    {
        pgMap [gainLabel].assign (kTableSize, 0.0f);
        pgCMap[gainLabel].assign (kTableSize, 0);
    }
    auto& pgAccum  = pgMap [gainLabel];
    auto& pgCounts = pgCMap[gainLabel];

    for (int n = startSample; n < startSample + available; ++n)
    {
        const float x = juce::jlimit (-1.0f, 1.0f, refPtr[n]);
        const int binIdx = juce::jlimit (0, kTableSize - 1,
                               (int) ((x + 1.0f) * 0.5f * (float) (kTableSize - 1)));
        const float y = recPtr[n] - dcOffset;
        wsAcc [binIdx] += y;
        wsCnts[binIdx] += 1;
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
void AnalysisEngine::finaliseWaveshaper (LNLModel& model, int channel)
{
    if (channel == 0)
        finaliseWaveshaperInto (model.waveshaper,  wsAccum,  wsCounts);
    else
        finaliseWaveshaperInto (model.waveshaperR, wsAccumR, wsCountsR);
}

//==============================================================================
// Build model.gainModels from per-gain-label accumulators.
// Each gain label becomes one GainModel with a separately finalised waveshaper.
// Models are sorted by the numeric value parsed from the gain label
// (e.g. "25%" → 25, "75%" → 75) and gainValue is normalised 0…1.
//==============================================================================
void AnalysisEngine::populateGainModels (LNLModel& model, int channel)
{
    auto& pgMap  = (channel == 0) ? perGainWsAccum  : perGainWsAccumR;
    auto& pgCMap = (channel == 0) ? perGainWsCounts : perGainWsCountsR;
    auto& dest   = (channel == 0) ? model.gainModels : model.gainModelsR;

    if (pgMap.empty())
        return;

    // Collect labels and their parsed numeric values for sorting.
    std::vector<std::pair<float, juce::String>> sortable;
    for (const auto& [label, _] : pgMap)
    {
        const float v = label.trimCharactersAtEnd ("%").getFloatValue();
        sortable.emplace_back (v, label);
    }
    std::sort (sortable.begin(), sortable.end(),
               [] (const auto& a, const auto& b) { return a.first < b.first; });

    const int N = (int) sortable.size();
    dest.clear();
    dest.reserve (N);

    for (int idx = 0; idx < N; ++idx)
    {
        const juce::String& label  = sortable[idx].second;
        GainModel gm;
        gm.gainLabel  = label;
        gm.gainValue  = (N > 1) ? (float) idx / (float) (N - 1) : 0.5f;
        finaliseWaveshaperInto (gm.waveshaper,
                                pgMap .at (label),
                                pgCMap.at (label));
        dest.push_back (std::move (gm));
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

//==============================================================================
// In-place lower-triangular Cholesky factorisation of a positive-definite n×n
// symmetric matrix A stored row-major in a flat vector (length n*n).
// On success the lower triangle of A is overwritten with L s.t. A = L Lᵀ.
// Returns false if any diagonal pivot is non-positive (matrix not PD).
//==============================================================================
bool AnalysisEngine::choleskyDecompose (std::vector<double>& A, int n)
{
    for (int j = 0; j < n; ++j)
    {
        double s = A[j * n + j];
        for (int k = 0; k < j; ++k)
            s -= A[j * n + k] * A[j * n + k];
        if (s <= 0.0) return false;
        A[j * n + j] = std::sqrt (s);

        const double Ljj = A[j * n + j];
        for (int i = j + 1; i < n; ++i)
        {
            double t = A[i * n + j];
            for (int k = 0; k < j; ++k)
                t -= A[i * n + k] * A[j * n + k];
            A[i * n + j] = t / Ljj;
        }
    }
    return true;
}

//==============================================================================
// Solve L Lᵀ x = b in-place (result overwrites b) given the lower-triangular
// Cholesky factor L produced by choleskyDecompose.
//==============================================================================
void AnalysisEngine::choleskySolve (const std::vector<double>& L,
                                     std::vector<double>& b, int n)
{
    // Forward substitution: L y = b
    for (int i = 0; i < n; ++i)
    {
        double s = b[i];
        for (int k = 0; k < i; ++k)
            s -= L[i * n + k] * b[k];
        b[i] = s / L[i * n + i];
    }
    // Back substitution: Lᵀ x = y
    for (int i = n - 1; i >= 0; --i)
    {
        double s = b[i];
        for (int k = i + 1; k < n; ++k)
            s -= L[k * n + i] * b[k];
        b[i] = s / L[i * n + i];
    }
}

//==============================================================================
// Identify second-order truncated Volterra kernels from one ref/rec pair via
// normal equations solved with Cholesky factorisation.
//
// The model is:
//   y[n] = Σ_{m=0}^{M1-1}   h1[m] x[n-m]
//         + Σ_{m1=0}^{M2-1} Σ_{m2=m1}^{M2-1}  h2[m1,m2] x[n-m1] x[n-m2]
//
// h2 is stored flat upper-triangular:
//   index(m1,m2) = m1*(2*M2 - m1 + 1)/2 + (m2 - m1)
//
// On success sets model.volterraH1/H2/M1/M2 and modelType = Volterra.
// Returns an error string on failure; model is unchanged in that case.
//==============================================================================
juce::String AnalysisEngine::identifyVolterraKernels (
    const juce::AudioBuffer<float>& ref,
    const juce::AudioBuffer<float>& rec,
    int numSamples,
    LNLModel& model,
    double sampleRate,
    int m1Taps,
    int m2Taps,
    int channel)
{
    const int startN = std::max (m1Taps, m2Taps);
    if (numSamples < startN + 1024)
        return "Volterra: not enough samples ("
               + juce::String (numSamples) + " available, "
               + juce::String (startN + 1024) + " required)";

    const int m2Pairs = m2Taps * (m2Taps + 1) / 2;
    const int P       = m1Taps + m2Pairs;   // total unknowns

    // Normal-equation accumulators in double precision.
    std::vector<double> Rxx (P * P, 0.0);
    std::vector<double> rxy (P,     0.0);
    std::vector<double> phi (P);

    const int refCh = juce::jlimit (0, ref.getNumChannels() - 1, channel);
    const int recCh = juce::jlimit (0, rec.getNumChannels() - 1, channel);
    const float* x = ref.getReadPointer (refCh);
    const float* y = rec.getReadPointer (recCh);

    for (int n = startN; n < numSamples; ++n)
    {
        // Linear regressors: x[n], x[n-1], ..., x[n-M1+1]
        for (int m = 0; m < m1Taps; ++m)
            phi[m] = x[n - m];

        // Quadratic regressors: x[n-m1]*x[n-m2], 0 ≤ m1 ≤ m2 < M2
        int idx = m1Taps;
        for (int m1 = 0; m1 < m2Taps; ++m1)
        {
            const double xm1 = x[n - m1];
            for (int m2 = m1; m2 < m2Taps; ++m2)
                phi[idx++] = xm1 * x[n - m2];
        }

        // Accumulate upper triangle of Rxx and full rxy.
        const double yn = y[n];
        for (int i = 0; i < P; ++i)
        {
            for (int j = i; j < P; ++j)
                Rxx[i * P + j] += phi[i] * phi[j];
            rxy[i] += phi[i] * yn;
        }
    }

    // Symmetrise from upper triangle.
    for (int i = 0; i < P; ++i)
        for (int j = i + 1; j < P; ++j)
            Rxx[j * P + i] = Rxx[i * P + j];

    // Tikhonov ridge: 1e-4 * mean diagonal keeps the matrix well-conditioned
    // when some quadratic terms have low excitation energy.
    double diagSum = 0.0;
    for (int i = 0; i < P; ++i) diagSum += Rxx[i * P + i];
    const double ridge = 1e-4 * diagSum / (double) P;
    for (int i = 0; i < P; ++i) Rxx[i * P + i] += ridge;

    if (! choleskyDecompose (Rxx, P))
        return "Volterra: normal-equation matrix is not positive definite "
               "(try shorter kernels or a better-excited capture)";

    choleskySolve (Rxx, rxy, P);
    // rxy now holds the solution [h1 (M1 entries) | h2_flat (m2Pairs entries)].

    // Normalize h1 to unity gain at 1 kHz so the waveshaper knee sits at the
    // same level as the LNL path (whose Wiener FIR is already 1 kHz-normalized).
    {
        const double omega1k = juce::MathConstants<double>::twoPi * 1000.0 / sampleRate;
        double re = 0.0, im = 0.0;
        for (int m = 0; m < m1Taps; ++m)
        {
            re += rxy[m] * std::cos (omega1k * m);
            im -= rxy[m] * std::sin (omega1k * m);
        }
        const double norm = std::sqrt (re * re + im * im);
        if (norm > 1e-6)
            for (int i = 0; i < P; ++i)
                rxy[i] /= norm;
    }

    if (channel == 0)
    {
        model.volterraM1 = m1Taps;
        model.volterraM2 = m2Taps;
        model.volterraH1.assign (rxy.begin(),          rxy.begin() + m1Taps);
        model.volterraH2.assign (rxy.begin() + m1Taps, rxy.end());
        model.modelType  = LNLModel::ModelType::Volterra;
    }
    else
    {
        model.volterraH1R.assign (rxy.begin(),          rxy.begin() + m1Taps);
        model.volterraH2R.assign (rxy.begin() + m1Taps, rxy.end());
    }

    return {};
}
