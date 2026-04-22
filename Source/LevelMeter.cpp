// Draws a vertical peak-hold / RMS bar meter in phosphor-green CRT style.
// Peak hold freezes for 1.5 s then decays at 0.5 dB per tick; updated at 10 Hz.

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

    g.fillAll (juce::Colour (0xff0d0d0d));

    const float labelW = 22.0f;
    const float barX   = labelW + 3.0f;
    const float barW   = w - barX - 3.0f;
    const float barTop = 6.0f;
    const float barBot = h - 6.0f;
    const float barH   = barBot - barTop;

    // --- RMS bar: dim green at bottom, bright green at top ---
    if (rmsDb > kMinDb)
    {
        const float norm = dbToNorm (rmsDb);
        const float topY = barBot - norm * barH;

        juce::ColourGradient grad (
            juce::Colour (0xff1a4d1a), barX, barBot,
            juce::Colour (0xff33ff33), barX, barTop,
            false);
        grad.addColour ((double) dbToNorm (-12.0f), juce::Colour (0xff1acc1a));

        g.setGradientFill (grad);
        g.fillRect (barX, topY, barW, barBot - topY);
    }

    // --- Peak hold marker ---
    {
        const float norm = dbToNorm (peakDb);
        const float py   = barBot - norm * barH;
        // Bright full-green at hot levels, mid-green otherwise
        const auto col = (peakDb > -6.0f)
            ? juce::Colour (0xff33ff33)
            : juce::Colour (0xff1acc1a);
        g.setColour (col);
        g.fillRect (barX, py - 1.0f, barW, 2.0f);
    }

    // --- dB scale ticks and labels ---
    g.setFont (juce::Font (juce::FontOptions().withName ("Courier New").withHeight (9.0f)));

    constexpr float kTicks[] = { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f };
    for (float db : kTicks)
    {
        const float ty = barBot - dbToNorm (db) * barH;

        g.setColour (juce::Colour (0x1a33ff33));
        g.drawHorizontalLine ((int) ty, barX, barX + barW);

        g.setColour (juce::Colour (0x6633ff33));
        const juce::String lbl = (db == 0.0f) ? "0" : juce::String ((int) db);
        g.drawText (lbl, 0, (int) ty - 5, (int) labelW, 10,
                    juce::Justification::centredRight, false);
    }

    // Bar outline
    g.setColour (juce::Colour (0x3033ff33));
    g.drawRect (barX, barTop, barW, barH, 1.0f);
}
