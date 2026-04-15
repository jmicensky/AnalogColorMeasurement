// Draws a vertical peak-hold / RMS bar meter with a green-to-red gradient and dB tick marks.
// Peak hold freezes for 1.5 s then decays at 0.5 dB per tick; updated at 10 Hz from the audio thread's leaky-integrator RMS.

#include "LevelMeter.h"

LevelMeter::LevelMeter()
{
    setOpaque (true);
}

void LevelMeter::setLevels (float peakLinear, float rmsLinear)
{
    const float newPeakDb = (peakLinear > 0.0f)
        ? juce::Decibels::gainToDecibels (peakLinear)
        : kMinDb;

    rmsDb = (rmsLinear > 0.0f)
        ? juce::Decibels::gainToDecibels (rmsLinear)
        : kMinDb;

    if (newPeakDb >= peakDb)
    {
        peakDb   = newPeakDb;
        holdTick = 0;
    }
    else
    {
        ++holdTick;
        if (holdTick > kHoldTicks)
            peakDb = std::max (kMinDb, peakDb - kDecayDbPer);
    }

    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const float w = (float) getWidth();
    const float h = (float) getHeight();

    // Background
    g.fillAll (juce::Colour (0xff0d0d0d));

    // Layout: narrow dB label column on the left, bar on the right.
    const float labelW = 22.0f;
    const float barX   = labelW + 3.0f;
    const float barW   = w - barX - 3.0f;
    const float barTop = 6.0f;
    const float barBot = h - 6.0f;
    const float barH   = barBot - barTop;

    // --- RMS bar with green→yellow→red gradient (bottom to top) ---
    if (rmsDb > kMinDb)
    {
        const float norm = dbToNorm (rmsDb);
        const float topY = barBot - norm * barH;

        juce::ColourGradient grad (
            juce::Colour (0xff00bb00), barX, barBot,
            juce::Colour (0xffdd0000), barX, barTop,
            false);
        // Yellow transition at -12 dB
        grad.addColour ((double) dbToNorm (-12.0f), juce::Colour (0xffdddd00));

        g.setGradientFill (grad);
        g.fillRect (barX, topY, barW, barBot - topY);
    }

    // --- Peak hold marker ---
    {
        const float norm = dbToNorm (peakDb);
        const float py   = barBot - norm * barH;
        const juce::Colour col = (peakDb > -6.0f)  ? juce::Colours::red
                               : (peakDb > -18.0f) ? juce::Colours::yellow
                                                   : juce::Colours::lightgreen;
        g.setColour (col);
        g.fillRect (barX, py - 1.0f, barW, 2.0f);
    }

    // --- dB scale ticks and labels ---
    g.setFont (juce::Font (juce::FontOptions().withHeight (9.0f)));

    constexpr float kTicks[] = { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f };
    for (float db : kTicks)
    {
        const float ty = barBot - dbToNorm (db) * barH;

        // Tick line across full bar width
        g.setColour (juce::Colour (0xff444444));
        g.drawHorizontalLine ((int) ty, barX, barX + barW);

        // Text label
        g.setColour (juce::Colours::grey);
        const juce::String lbl = (db == 0.0f) ? "0" : juce::String ((int) db);
        g.drawText (lbl, 0, (int) ty - 5, (int) labelW, 10,
                    juce::Justification::centredRight, false);
    }

    // Bar outline
    g.setColour (juce::Colours::darkgrey);
    g.drawRect (barX, barTop, barW, barH, 1.0f);
}
