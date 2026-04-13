#pragma once
#include <JuceHeader.h>

enum class CaptureQuality { Quick, Standard, HiFi };

namespace StimulusNames
{
    // All available stimuli.
    // DRUMS / BASSDI / ElecKeys are loaded from embedded binary data.
    // SineSweep / PinkNoise / Sine1kHz / Sine100Hz are generated at runtime.
    static const juce::StringArray all {
        "DRUMS", "BASSDI", "ElecKeys",
        "SineSweep", "PinkNoise", "Sine1kHz", "Sine100Hz"
    };

    // Returns the stimulus list for the chosen capture quality.
    inline juce::StringArray forQuality (CaptureQuality q)
    {
        switch (q)
        {
            case CaptureQuality::Quick:
                // 2 stimuli — fast sanity check / first-pass
                return { "SineSweep", "Sine1kHz" };

            case CaptureQuality::Standard:
                // 4 stimuli — good balance of speed and coverage
                return { "SineSweep", "Sine1kHz", "Sine100Hz", "DRUMS" };

            case CaptureQuality::HiFi:
                // 5 stimuli — full tonal + pink noise + musical excerpt
                return { "SineSweep", "PinkNoise", "Sine1kHz", "Sine100Hz", "DRUMS" };
        }
        return all;
    }
}

struct StimulusStep
{
    juce::String gainLabel;    // user-supplied label for the hardware setting, e.g. "-18dBu"
    juce::String stimulusName; // one of StimulusNames::all
    bool         completed { false };
};

struct MarkerEntry
{
    int          stepIndex;
    juce::String gainLabel;
    juce::String stimulusName;
    juce::String refFile;      // filename only, e.g. "MyProject_sat_-18dBu_DRUMS_ref.wav"
    juce::String recFile;
    juce::String timestamp;    // ISO 8601
};
