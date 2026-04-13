#pragma once
#include <JuceHeader.h>
#include "MeasurementTypes.h"

class StimulusPlan
{
public:
    StimulusPlan() = default;

    // Builds steps from a comma-separated list of gain labels and a quality level.
    // E.g. gainsCsv = "-18, -12, -6, 0, +6", quality = Standard
    //      → 20 steps (5 gains × 4 stimuli).
    void build (const juce::String& gainsCsv,
                CaptureQuality quality = CaptureQuality::Standard);

    bool isEmpty()    const { return steps.isEmpty(); }
    bool isComplete() const { return currentIndex >= steps.size(); }
    int  totalSteps() const { return steps.size(); }
    int  currentStepIndex() const { return currentIndex; }

    // Returns nullptr if the plan is empty or complete.
    const StimulusStep* getCurrentStep() const;

    // Marks the current step completed and advances to the next.
    void advance();

    void reset();

    const juce::Array<StimulusStep>& getSteps() const { return steps; }

private:
    juce::Array<StimulusStep> steps;
    int currentIndex { 0 };
};
