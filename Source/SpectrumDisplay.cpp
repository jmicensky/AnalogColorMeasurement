// Renders a logarithmic frequency-response curve (20 Hz–20 kHz, ±8 dB) with dB grid lines and frequency axis labels.
// Used in both the profiler app and plugin editor to visualize the hardware's measured tonal character.

#include "SpectrumDisplay.h"

SpectrumDisplay::SpectrumDisplay()
{
    // Mark opaque so plugin hosts don't try to composite the parent background
    // behind this component — it paints every pixel itself via fillAll.
    setOpaque (true);
}

void SpectrumDisplay::setData (const std::vector<float>& freqHz,
                                const std::vector<float>& magDb)
{
    freqs = freqHz;
    mags  = magDb;
    repaint();
}

void SpectrumDisplay::setShowSetupHints (bool show)
{
    showSetupHints = show;
    repaint();
}

//==============================================================================
float SpectrumDisplay::freqToNorm (float hz) noexcept
{
    if (hz <= 0.0f) hz = kMinFreq;
    return std::log (hz / kMinFreq) / std::log (kMaxFreq / kMinFreq);
}

float SpectrumDisplay::dbToNorm (float db) noexcept
{
    // 0 = top (max dB), 1 = bottom (min dB)
    return (kMaxDb - db) / (kMaxDb - kMinDb);
}

//==============================================================================
void SpectrumDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float left   = 42.0f;
    const float right  = bounds.getWidth() - 8.0f;
    const float top    = 8.0f;
    const float bottom = bounds.getHeight() - 22.0f;
    const float plotW  = right - left;
    const float plotH  = bottom - top;

    // --- Component background + outer border so it stands out ---
    g.fillAll (juce::Colour (0xff1a1a2a));
    g.setColour (juce::Colour (0xff5050a0));
    g.drawRect (getLocalBounds(), 1);

    // --- Plot area background ---
    g.setColour (juce::Colour (0xff0f0f1c));
    g.fillRect (left, top, plotW, plotH);

    // --- dB grid lines ---
    const float dbGrid[] = { -8.0f, -6.0f, -4.0f, -2.0f, 0.0f, 2.0f, 4.0f, 6.0f };
    g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));

    for (float db : dbGrid)
    {
        const float y = top + dbToNorm (db) * plotH;

        if (db == 0.0f)
        {
            g.setColour (juce::Colour (0xff505070));
            g.drawHorizontalLine (juce::roundToInt (y), left, right);
        }
        else
        {
            g.setColour (juce::Colour (0xff303048));
            g.drawHorizontalLine (juce::roundToInt (y), left, right);
        }

        // Y-axis label
        g.setColour (juce::Colour (0xff8080a0));
        const juce::String label = (db > 0 ? "+" : "") + juce::String ((int) db);
        g.drawText (label, 0, juce::roundToInt (y) - 6, (int) left - 2, 13,
                    juce::Justification::centredRight);
    }

    // --- Frequency grid lines ---
    const float freqGrid[] = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
                                1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
    const juce::String freqLabels[] = { "20", "50", "100", "200", "500",
                                        "1k", "2k", "5k", "10k", "20k" };

    for (int i = 0; i < 10; ++i)
    {
        const float x = left + freqToNorm (freqGrid[i]) * plotW;

        g.setColour (juce::Colour (0xff303048));
        g.drawVerticalLine (juce::roundToInt (x), top, bottom);

        g.setColour (juce::Colour (0xff8080a0));
        g.drawText (freqLabels[i],
                    juce::roundToInt (x) - 16, (int) bottom + 2, 32, 16,
                    juce::Justification::centred);
    }

    // --- Plot border ---
    g.setColour (juce::Colour (0xff404060));
    g.drawRect (left, top, plotW, plotH, 1.0f);

    // --- FR curve ---
    if (freqs.size() < 2)
    {
        if (showSetupHints)
        {
            // Semi-transparent instructional overlay for first-time setup.
            const float boxW   = std::min (plotW - 40.0f, 520.0f);
            const float innerW = boxW - 36.0f;
            const float padX   = left + (plotW - boxW) * 0.5f + 18.0f;

            // Measure each hint at 12.5 pt with word-wrap to compute exact height needed.
            const juce::String hints[] = {
                u8"\u2022  Set input gain with the hardware at its loudest/most-driven setting \u2014 the plan starts there intentionally.",
                u8"\u2022  Use a reamp box between your line output and the hardware input for correct impedance and level.",
                u8"\u2022  Do NOT adjust the interface input gain between capture steps \u2014 all levels must stay fixed.",
                u8"\u2022  Send and Return must be on the same physical interface so latency alignment works correctly."
            };

            const float hintFontH = 12.5f;
            const float lineH     = hintFontH + 3.0f;   // px per text line
            const float hintGap   = 8.0f;               // vertical gap between hints
            const float titleH    = 18.0f;
            const float titleGap  = 12.0f;
            const float padTop    = 14.0f;
            const float padBot    = 14.0f;

            // Compute total height by counting wrapped lines per hint.
            juce::Font hintFont (juce::FontOptions().withHeight (hintFontH));
            float contentH = padTop + titleH + titleGap;
            for (int i = 0; i < 4; ++i)
            {
                juce::AttributedString as;
                as.append (hints[i], hintFont, juce::Colours::white);
                as.setWordWrap (juce::AttributedString::byWord);
                juce::TextLayout tl;
                tl.createLayout (as, innerW);
                contentH += tl.getNumLines() * lineH + hintGap;
            }
            contentH += padBot - hintGap;  // replace last gap with bottom pad

            const float boxH = contentH;
            const float boxX = left + (plotW - boxW) * 0.5f;
            const float boxY = top  + (plotH - boxH) * 0.5f;

            g.setColour (juce::Colour (0xcc333340));   // ~80% opaque dark grey
            g.fillRoundedRectangle (boxX, boxY, boxW, boxH, 8.0f);
            g.setColour (juce::Colour (0x66aaaacc));
            g.drawRoundedRectangle (boxX, boxY, boxW, boxH, 8.0f, 1.0f);

            float ty = boxY + padTop;

            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f).withStyle ("Bold")));
            g.drawText ("Interface Setup — Before You Measure", (int) padX, (int) ty,
                        (int) innerW, (int) titleH, juce::Justification::centredLeft);
            ty += titleH + titleGap;

            g.setFont (hintFont);
            g.setColour (juce::Colours::lightgrey.withAlpha (0.9f));

            for (const auto& hint : hints)
            {
                juce::AttributedString as;
                as.append (hint, hintFont, juce::Colours::lightgrey.withAlpha (0.9f));
                as.setWordWrap (juce::AttributedString::byWord);
                juce::TextLayout tl;
                tl.createLayout (as, innerW);
                const float blockH = tl.getNumLines() * lineH;
                tl.draw (g, juce::Rectangle<float> (padX, ty, innerW, blockH));
                ty += blockH + hintGap;
            }
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
            g.drawText ("No model loaded — load a .json artifact file",
                        juce::roundToInt (left), juce::roundToInt (top),
                        juce::roundToInt (plotW), juce::roundToInt (plotH),
                        juce::Justification::centred);
        }
        return;
    }

    juce::Path curve;
    bool started = false;

    for (size_t i = 0; i < freqs.size(); ++i)
    {
        const float f  = freqs[i];
        const float db = mags[i];

        if (f < kMinFreq || f > kMaxFreq) continue;

        const float x = left + freqToNorm (f) * plotW;
        const float y = top  + juce::jlimit (0.0f, 1.0f, dbToNorm (db)) * plotH;

        if (! started) { curve.startNewSubPath (x, y); started = true; }
        else           { curve.lineTo (x, y); }
    }

    // Filled area under curve (subtle)
    if (started)
    {
        juce::Path fill = curve;
        fill.lineTo (right, bottom);
        fill.lineTo (left,  bottom);
        fill.closeSubPath();

        g.setColour (juce::Colour (0x2200d4aa));
        g.fillPath (fill);
    }

    // Main curve line
    g.setColour (juce::Colour (0xff00d4aa));
    g.strokePath (curve, juce::PathStrokeType (1.5f));
}
