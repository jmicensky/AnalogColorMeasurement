#pragma once
#include <JuceHeader.h>

// Vertical peak-hold + RMS level meter.
// Call setLevels() from a 100ms timer; peak hold and decay are handled internally.
class LevelMeter : public juce::Component
{
public:
    LevelMeter();
    void paint (juce::Graphics&) override;

    // Call from a ~100ms timer.  Both values are linear (0..1).
    void setLevels (float peakLinear, float rmsLinear);

private:
    static constexpr float kMinDb      = -60.0f;
    static constexpr float kMaxDb      =   0.0f;
    static constexpr int   kHoldTicks  = 15;    // 15 × 100ms = 1.5s hold
    static constexpr float kDecayDbPer =  0.5f; // dB per tick during decay

    // Maps dB value to a 0 (bottom) … 1 (top) normalised position.
    float dbToNorm (float db) const noexcept
    {
        return juce::jlimit (0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
    }

    float peakDb   { kMinDb };
    float rmsDb    { kMinDb };
    int   holdTick { kHoldTicks + 1 };  // starts in decay mode

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
};
