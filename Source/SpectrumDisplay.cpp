// Renders a logarithmic frequency-response curve (20 Hz–20 kHz, ±8 dB) with a phosphor-green
// CRT aesthetic: raster grid, rounded display bezel, and multi-layer glow on the curve.

#include "SpectrumDisplay.h"

namespace
{
    constexpr uint32_t kGreenPrimary = 0xff33ff33;
    constexpr uint32_t kGreenDim     = 0x6633ff33;
    constexpr uint32_t kGreenGrid    = 0x0f33ff33;  // ~6% — raster squares
    constexpr uint32_t kGreenLine    = 0x1a33ff33;  // ~10% — dB/freq grid
    constexpr uint32_t kGreenZero    = 0x3f33ff33;  // 0 dB line, slightly brighter
    constexpr uint32_t kBgBase       = 0xff0d0d0d;
    constexpr uint32_t kBgPlot       = 0xff080808;
}

SpectrumDisplay::SpectrumDisplay()
{
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
    return (kMaxDb - db) / (kMaxDb - kMinDb);
}

//==============================================================================
void SpectrumDisplay::paint (juce::Graphics& g)
{
    const auto  bounds = getLocalBounds().toFloat();
    const float left   = 42.0f;
    const float right  = bounds.getWidth() - 8.0f;
    const float top    = 8.0f;
    const float bottom = bounds.getHeight() - 22.0f;
    const float plotW  = right - left;
    const float plotH  = bottom - top;

    // --- Component background ---
    g.fillAll (juce::Colour (kBgBase));

    // --- CRT bezel: recessed display with 20px corner radius ---
    const juce::Rectangle<float> bezel (left - 6.0f, top - 6.0f,
                                        plotW + 12.0f, plotH + 12.0f);
    g.setColour (juce::Colour (kBgPlot));
    g.fillRoundedRectangle (bezel, 20.0f);

    // Outer glow ring
    g.setColour (juce::Colour (0x2033ff33));
    g.drawRoundedRectangle (bezel.reduced (0.5f), 20.0f, 1.5f);

    // Inner plot fill
    g.setColour (juce::Colour (kBgPlot));
    g.fillRect (left, top, plotW, plotH);

    // --- Raster grid (40 px squares) ---
    g.setColour (juce::Colour (kGreenGrid));
    for (float x = left; x <= right; x += 40.0f)
        g.drawVerticalLine (juce::roundToInt (x), top, bottom);
    for (float y = top; y <= bottom; y += 40.0f)
        g.drawHorizontalLine (juce::roundToInt (y), left, right);

    // --- dB grid lines + Y-axis labels ---
    const float dbGrid[] = { -8.0f, -6.0f, -4.0f, -2.0f, 0.0f, 2.0f, 4.0f, 6.0f };
    g.setFont (juce::Font (juce::FontOptions().withName ("Courier New").withHeight (10.0f)));

    for (float db : dbGrid)
    {
        const float y = top + dbToNorm (db) * plotH;

        g.setColour (juce::Colour (db == 0.0f ? kGreenZero : kGreenLine));
        g.drawHorizontalLine (juce::roundToInt (y), left, right);

        g.setColour (juce::Colour (kGreenDim));
        const juce::String label = (db > 0 ? "+" : "") + juce::String ((int) db);
        g.drawText (label, 0, juce::roundToInt (y) - 6, (int) left - 2, 13,
                    juce::Justification::centredRight);
    }

    // --- Frequency grid lines + X-axis labels ---
    const float freqGrid[]       = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
                                     1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
    const juce::String freqLabels[] = { "20", "50", "100", "200", "500",
                                        "1k", "2k", "5k", "10k", "20k" };

    for (int i = 0; i < 10; ++i)
    {
        const float x = left + freqToNorm (freqGrid[i]) * plotW;

        g.setColour (juce::Colour (kGreenLine));
        g.drawVerticalLine (juce::roundToInt (x), top, bottom);

        g.setColour (juce::Colour (kGreenDim));
        g.drawText (freqLabels[i],
                    juce::roundToInt (x) - 16, (int) bottom + 2, 32, 16,
                    juce::Justification::centred);
    }

    // --- Plot border ---
    g.setColour (juce::Colour (0x3033ff33));
    g.drawRect (left, top, plotW, plotH, 1.0f);

    // --- No-data state ---
    if (freqs.size() < 2)
    {
        if (showSetupHints)
        {
            const float boxW   = std::min (plotW - 40.0f, 520.0f);
            const float innerW = boxW - 36.0f;
            const float padX   = left + (plotW - boxW) * 0.5f + 18.0f;

            const juce::String hints[] = {
                u8"•  Set input gain with the hardware at its loudest/most-driven setting — the plan starts there intentionally.",
                u8"•  Use a reamp box between your line output and the hardware input for correct impedance and level.",
                u8"•  Do NOT adjust the interface input gain between capture steps — all levels must stay fixed.",
                u8"•  Send and Return must be on the same physical interface so latency alignment works correctly."
            };

            const float hintFontH = 12.5f;
            const float lineH     = hintFontH + 3.0f;
            const float hintGap   = 8.0f;
            const float titleH    = 18.0f;
            const float titleGap  = 12.0f;
            const float padTop    = 14.0f;
            const float padBot    = 14.0f;

            juce::Font hintFont (juce::FontOptions().withName ("Courier New").withHeight (hintFontH));
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
            contentH += padBot - hintGap;

            const float boxH = contentH;
            const float boxX = left + (plotW - boxW) * 0.5f;
            const float boxY = top  + (plotH - boxH) * 0.5f;

            g.setColour (juce::Colour (0xcc0d1a0d));
            g.fillRoundedRectangle (boxX, boxY, boxW, boxH, 8.0f);
            g.setColour (juce::Colour (kGreenDim));
            g.drawRoundedRectangle (boxX, boxY, boxW, boxH, 8.0f, 1.0f);

            float ty = boxY + padTop;

            g.setColour (juce::Colour (kGreenPrimary));
            g.setFont (juce::Font (juce::FontOptions().withName ("Courier New")
                                                      .withHeight (13.0f)
                                                      .withStyle ("Bold")));
            g.drawText ("Interface Setup \xe2\x80\x94 Before You Measure",
                        (int) padX, (int) ty, (int) innerW, (int) titleH,
                        juce::Justification::centredLeft);
            ty += titleH + titleGap;

            g.setFont (hintFont);

            for (const auto& hint : hints)
            {
                juce::AttributedString as;
                as.append (hint, hintFont, juce::Colour (kGreenDim));
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
            g.setColour (juce::Colour (kGreenDim));
            g.setFont (juce::Font (juce::FontOptions().withName ("Courier New").withHeight (14.0f)));
            g.drawText ("No model loaded \xe2\x80\x94 load a .json artifact file",
                        juce::roundToInt (left), juce::roundToInt (top),
                        juce::roundToInt (plotW), juce::roundToInt (plotH),
                        juce::Justification::centred);
        }

        // Scanlines over plot area even with no data
        g.setColour (juce::Colour (0x09000000));
        for (float y = top; y < bottom; y += 2.0f)
            g.drawHorizontalLine (juce::roundToInt (y), left, right);
        return;
    }

    // --- Build curve path ---
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

    if (started)
    {
        // Filled area under curve
        juce::Path fill = curve;
        fill.lineTo (right, bottom);
        fill.lineTo (left,  bottom);
        fill.closeSubPath();
        g.setColour (juce::Colour (0x1233ff33));
        g.fillPath (fill);

        // Glow layers (wide → narrow, dim → bright)
        g.setColour (juce::Colour (0x1a33ff33));
        g.strokePath (curve, juce::PathStrokeType (5.0f));

        g.setColour (juce::Colour (0x4033ff33));
        g.strokePath (curve, juce::PathStrokeType (2.5f));

        // Main curve
        g.setColour (juce::Colour (kGreenPrimary));
        g.strokePath (curve, juce::PathStrokeType (1.5f));
    }

    // --- Scanlines clipped to plot area ---
    g.setColour (juce::Colour (0x09000000));
    for (float y = top; y < bottom; y += 2.0f)
        g.drawHorizontalLine (juce::roundToInt (y), left, right);
}
