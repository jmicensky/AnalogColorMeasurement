// Builds and manages an ordered list of (gain level, stimulus) capture steps for a measurement session.
// Steps are sorted highest drive level first so A/D gain staging can be set once against the loudest step.

#include "StimulusPlan.h"

void StimulusPlan::build (const juce::String& gainsCsv, CaptureQuality quality)
{
    steps.clear();
    currentIndex = 0;

    const juce::StringArray stimuli = StimulusNames::forQuality (quality);
    auto tokens = juce::StringArray::fromTokens (gainsCsv, ",", "");

    // Always capture highest drive level first so gain staging can be set
    // once against the loudest/most saturated step without readjustment.
    for (int i = 0, j = tokens.size() - 1; i < j; ++i, --j)
        std::swap (tokens.getReference (i), tokens.getReference (j));

    for (const auto& token : tokens)
    {
        const juce::String label = token.trim();
        if (label.isEmpty())
            continue;

        for (const auto& stimName : stimuli)
        {
            StimulusStep step;
            step.gainLabel    = label;
            step.stimulusName = stimName;
            steps.add (step);
        }
    }
}

const StimulusStep* StimulusPlan::getCurrentStep() const
{
    if (isComplete() || isEmpty())
        return nullptr;
    return &steps.getReference (currentIndex);
}

void StimulusPlan::advance()
{
    if (currentIndex < steps.size())
    {
        steps.getReference (currentIndex).completed = true;
        ++currentIndex;
    }
}

void StimulusPlan::reset()
{
    currentIndex = 0;
    for (auto& s : steps)
        s.completed = false;
}
