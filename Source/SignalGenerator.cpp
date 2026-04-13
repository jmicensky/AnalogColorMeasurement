#include "SignalGenerator.h"

//==============================================================================
// Shared helper: wrap an AudioBuffer<float> (mono) into a 24-bit WAV MemoryBlock.
//==============================================================================
juce::MemoryBlock SignalGenerator::writeMonoWav (const juce::AudioBuffer<float>& buffer,
                                                  double sampleRate)
{
    juce::MemoryBlock result;
    juce::WavAudioFormat wav;

    std::unique_ptr<juce::OutputStream> stream =
        std::make_unique<juce::MemoryOutputStream> (result, false);

    auto writer = std::unique_ptr<juce::AudioFormatWriter> (
        wav.createWriterFor (stream,
                             juce::AudioFormatWriterOptions{}
                                 .withSampleRate    (sampleRate)
                                 .withNumChannels   ((unsigned int) buffer.getNumChannels())
                                 .withBitsPerSample (24)));

    if (writer != nullptr)
    {
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        writer->flush();
    }

    return result;
}

//==============================================================================
// Logarithmic sine sweep  (20 Hz → 20 kHz)
//==============================================================================
juce::MemoryBlock SignalGenerator::makeLogSineSweep (float durationSecs,
                                                      double sampleRate,
                                                      float amplitude)
{
    const int numSamples = juce::roundToInt (durationSecs * sampleRate);
    juce::AudioBuffer<float> buf (1, numSamples);
    float* data = buf.getWritePointer (0);

    constexpr double f1 = 20.0;
    constexpr double f2 = 20000.0;
    const double T  = durationSecs;
    const double K  = T / std::log (f2 / f1);
    const double L  = std::log (f2 / f1);

    for (int n = 0; n < numSamples; ++n)
    {
        const double t     = (double) n / sampleRate;
        const double phase = juce::MathConstants<double>::twoPi * K * f1 * (std::exp (t * L / T) - 1.0);
        data[n] = amplitude * (float) std::sin (phase);
    }

    // 5 ms fade-in / fade-out to avoid clicks.
    const int fadeLen = juce::jmin (numSamples / 2, (int) (0.005 * sampleRate));
    for (int n = 0; n < fadeLen; ++n)
    {
        float gain = (float) n / (float) fadeLen;
        data[n]                     *= gain;
        data[numSamples - 1 - n]    *= gain;
    }

    return writeMonoWav (buf, sampleRate);
}

//==============================================================================
// Pink noise  — Voss-McCartney algorithm with 16 generators
//==============================================================================
juce::MemoryBlock SignalGenerator::makePinkNoise (float durationSecs,
                                                   double sampleRate,
                                                   float amplitude)
{
    const int numSamples = juce::roundToInt (durationSecs * sampleRate);
    juce::AudioBuffer<float> buf (1, numSamples);
    float* data = buf.getWritePointer (0);

    juce::Random rng;
    constexpr int numGenerators = 16;
    float generators[numGenerators] = {};
    float runningSum = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        // Find the lowest set bit of (n+1) to determine which generator to update.
        const int bit = __builtin_ctz ((uint32_t)(n + 1)) % numGenerators;
        runningSum  -= generators[bit];
        generators[bit] = (float) rng.nextDouble() * 2.0f - 1.0f;
        runningSum  += generators[bit];

        // White noise contribution.
        const float white = (float) rng.nextDouble() * 2.0f - 1.0f;
        data[n] = amplitude * (runningSum + white) / (float)(numGenerators + 1);
    }

    // Normalise to ± amplitude.
    float peak = 0.0f;
    for (int n = 0; n < numSamples; ++n)
        peak = std::max (peak, std::abs (data[n]));
    if (peak > 0.0f)
        buf.applyGain (amplitude / peak);

    return writeMonoWav (buf, sampleRate);
}

//==============================================================================
// Pure sine tone
//==============================================================================
juce::MemoryBlock SignalGenerator::makeSineTone (float frequencyHz,
                                                  float durationSecs,
                                                  double sampleRate,
                                                  float amplitude)
{
    const int numSamples = juce::roundToInt (durationSecs * sampleRate);
    juce::AudioBuffer<float> buf (1, numSamples);
    float* data = buf.getWritePointer (0);

    const double phaseIncrement = juce::MathConstants<double>::twoPi
                                  * (double) frequencyHz / sampleRate;
    double phase = 0.0;

    for (int n = 0; n < numSamples; ++n)
    {
        data[n] = amplitude * (float) std::sin (phase);
        phase  += phaseIncrement;
        if (phase >= juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;
    }

    // 5 ms fade-in / fade-out to avoid clicks.
    const int fadeLen = juce::jmin (numSamples / 2, (int) (0.005 * sampleRate));
    for (int n = 0; n < fadeLen; ++n)
    {
        float gain = (float) n / (float) fadeLen;
        data[n]                  *= gain;
        data[numSamples - 1 - n] *= gain;
    }

    return writeMonoWav (buf, sampleRate);
}
