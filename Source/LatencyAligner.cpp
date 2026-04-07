#include "LatencyAligner.h"

int LatencyAligner::findLatencySamples (const juce::AudioBuffer<float>& ref,
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
        return 0;

    return peakIdx;
}
