#include "StimulusPlan.h"

void StimulusPlan::build (const juce::String& gainsCsv, CaptureQuality quality)
{
    steps.clear();
    currentIndex = 0;

    const juce::StringArray stimuli = StimulusNames::forQuality (quality);
    auto tokens = juce::StringArray::fromTokens (gainsCsv, ",", "");

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
