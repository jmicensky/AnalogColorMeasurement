#include "LatencyAligner.h"

float LatencyAligner::findLatencySamples (const juce::AudioBuffer<float>& ref,
                                         const juce::AudioBuffer<float>& rec,
                                         int   numCapturedSamples,
                                         float sampleRate)
{
    // Use at most 2 seconds of signal for the correlation window.
    // Hardware round-trip latency is typically well under 500ms, so this is
    // more than sufficient while keeping FFT sizes manageable.
    const int windowSamples = juce::jmin (numCapturedSamples,
                                          (int) (2.0f * sampleRate));

    if (windowSamples < 64)
        return 0;

    // --- Build mono sums for ref and rec ---
    std::vector<float> refMono (windowSamples, 0.0f);
    std::vector<float> recMono (windowSamples, 0.0f);

    for (int ch = 0; ch < ref.getNumChannels(); ++ch)
    {
        const float* src = ref.getReadPointer (ch);
        for (int i = 0; i < windowSamples; ++i)
            refMono[i] += src[i];
    }

    for (int ch = 0; ch < rec.getNumChannels(); ++ch)
    {
        const float* src = rec.getReadPointer (ch);
        for (int i = 0; i < windowSamples; ++i)
            recMono[i] += src[i];
    }

    // --- FFT size: next power of 2 at or above 2 * windowSamples ---
    // This prevents circular aliasing in the cross-correlation result.
    int order = 1;
    while ((1 << order) < 2 * windowSamples)
        ++order;

    const int fftSize = 1 << order;
    juce::dsp::FFT fft (order);

    // Interleaved complex buffers — JUCE FFT requires size = 2 * fftSize.
    // Real input goes in the first fftSize elements; imaginary part is zero.
    std::vector<float> refFreq (2 * fftSize, 0.0f);
    std::vector<float> recFreq (2 * fftSize, 0.0f);

    for (int i = 0; i < windowSamples; ++i)
    {
        refFreq[i] = refMono[i];
        recFreq[i] = recMono[i];
    }

    fft.performRealOnlyForwardTransform (refFreq.data());
    fft.performRealOnlyForwardTransform (recFreq.data());

    // Cross-correlation in the frequency domain:
    //   xcorr[k] = ref[k] * conj(rec[k])
    // After IFFT, xcorr[τ] peaks where rec is delayed by τ samples vs ref.
    std::vector<float> xcorrFreq (2 * fftSize, 0.0f);
    for (int k = 0; k < fftSize; ++k)
    {
        const float re_a = refFreq[2 * k],     im_a = refFreq[2 * k + 1];
        const float re_b = recFreq[2 * k],     im_b = recFreq[2 * k + 1];
        // a * conj(b) = (re_a*re_b + im_a*im_b) + i*(im_a*re_b - re_a*im_b)
        xcorrFreq[2 * k]     = re_a * re_b + im_a * im_b;
        xcorrFreq[2 * k + 1] = im_a * re_b - re_a * im_b;
    }

    fft.performRealOnlyInverseTransform (xcorrFreq.data());

    // --- Find the peak within ±500ms ---
    // Positive lags [0..maxLag]: rec is delayed by that many samples.
    // Negative lags [fftSize-maxLag..fftSize-1]: rec leads (unusual for hardware).
    const int maxLag = (int) (0.5f * sampleRate);

    float peakVal = std::numeric_limits<float>::lowest();
    int   peakIdx = 0;

    for (int i = 0; i <= maxLag && i < fftSize; ++i)
    {
        if (xcorrFreq[i] > peakVal)
        {
            peakVal = xcorrFreq[i];
            peakIdx = i;
        }
    }

    for (int i = fftSize - maxLag; i < fftSize; ++i)
    {
        if (xcorrFreq[i] > peakVal)
        {
            peakVal = xcorrFreq[i];
            peakIdx = i - fftSize;   // negative lag
        }
    }

    // A negative result (rec leads ref) is physically unexpected for a
    // hardware loopback path — return 0 so we don't trim the wrong end.
    if (peakIdx < 0)
        return 0.0f;

    // --- Parabolic interpolation for sub-sample precision (eq. 11) ---
    // Fit a parabola through c[p-1], c[p], c[p+1] and find its true peak.
    float fracOffset = 0.0f;
    if (peakIdx > 0 && peakIdx < fftSize - 1)
    {
        const float cm1 = xcorrFreq[peakIdx - 1];
        const float c0  = xcorrFreq[peakIdx];
        const float cp1 = xcorrFreq[peakIdx + 1];
        const float denom = cm1 - 2.0f * c0 + cp1;
        if (std::abs (denom) > 1e-10f)
            fracOffset = 0.5f * (cm1 - cp1) / denom;

        // Clamp to ±0.5 sample (result outside this range means bad parabola fit).
        fracOffset = juce::jlimit (-0.5f, 0.5f, fracOffset);
    }

    return (float) peakIdx + fracOffset;
}

//==============================================================================
void LatencyAligner::applyFractionalDelay (juce::AudioBuffer<float>& buf,
                                            int numSamples,
                                            float fracDelay)
{
    if (fracDelay < 0.001f) return;   // negligible — skip

    // 4-point Lagrange interpolation: output[n] = buf[n + fracDelay]
    // Uses nodes at offsets 0, 1, 2, 3 relative to n, giving exactly
    // fracDelay samples of advance with no additional integer-sample shift.
    const float d  = fracDelay;
    const float L0 = (d - 1.0f) * (d - 2.0f) * (d - 3.0f) / (-6.0f);
    const float L1 = d           * (d - 2.0f) * (d - 3.0f) / ( 2.0f);
    const float L2 = d           * (d - 1.0f) * (d - 3.0f) / (-2.0f);
    const float L3 = d           * (d - 1.0f) * (d - 2.0f) / ( 6.0f);

    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        float* data = buf.getWritePointer (ch);

        // Process in a temporary buffer to avoid read-after-write artefacts.
        std::vector<float> out (numSamples);
        for (int n = 0; n < numSamples; ++n)
        {
            const float s0 = data[n];
            const float s1 = (n + 1 < numSamples) ? data[n + 1] : 0.0f;
            const float s2 = (n + 2 < numSamples) ? data[n + 2] : 0.0f;
            const float s3 = (n + 3 < numSamples) ? data[n + 3] : 0.0f;
            out[n] = L0 * s0 + L1 * s1 + L2 * s2 + L3 * s3;
        }

        std::copy (out.begin(), out.end(), data);
    }
}
