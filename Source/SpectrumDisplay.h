#pragma once
#include <JuceHeader.h>

// Reusable frequency-response display.
// Call setData() to update the curve; the component repaints itself.
// X-axis: log scale 20 Hz – 20 kHz.
// Y-axis: -8 dB – +6 dB.
class SpectrumDisplay : public juce::Component
{
public:
    SpectrumDisplay();

    // Set (or clear) the FR curve.  Vectors must be the same length.
    // Pass empty vectors to show the placeholder / setup hints overlay.
    void setData (const std::vector<float>& freqHz,
                  const std::vector<float>& magDb);

    // When true, the empty-state overlay shows setup instructions instead of
    // a plain "no model" message.  Enable in the profiler app; leave false in
    // the plugin editor.  Automatically hidden once real data is supplied.
    void setShowSetupHints (bool show);

    void paint (juce::Graphics&) override;

private:
    // Maps a frequency value to a normalised [0,1] x position (log scale).
    static float freqToNorm (float hz) noexcept;

    // Maps a dB value to a normalised [0,1] y position (0 = top).
    static float dbToNorm (float db) noexcept;

    std::vector<float> freqs;
    std::vector<float> mags;
    bool showSetupHints { false };

    static constexpr float kMinFreq  =   20.0f;
    static constexpr float kMaxFreq  = 20000.0f;
    static constexpr float kMinDb    =  -8.0f;
    static constexpr float kMaxDb    =   6.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumDisplay)
};
