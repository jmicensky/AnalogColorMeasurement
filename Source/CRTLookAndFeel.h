#pragma once
#include <JuceHeader.h>

// Phosphor-green CRT terminal aesthetic for the Hardware Profiler UI.
class CRTLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Palette (0xAARRGGBB)
    static constexpr uint32_t kColGreenPrimary = 0xff33ff33;  // #33FF33 full glow
    static constexpr uint32_t kColGreenDim     = 0x6633ff33;  // ~40% alpha
    static constexpr uint32_t kColGreenSubtle  = 0x1a33ff33;  // ~10% alpha, for grids/glows
    static constexpr uint32_t kColBgBase       = 0xff0d0d0d;  // #0D0D0D
    static constexpr uint32_t kColBgContainer  = 0xff121212;  // #121212
    static constexpr uint32_t kColBgSidebar    = 0xff151515;  // #151515
    static constexpr uint32_t kColBgButtonOff  = 0xff1a1a1a;
    static constexpr uint32_t kColBgButtonOn   = 0xff0d1f0d;  // dark green tint

    CRTLookAndFeel();

    static juce::Font monoFont (float height);

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont   (juce::ComboBox&) override;
    juce::Font getLabelFont      (juce::Label&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour&,
                               bool isHighlighted, bool isDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool isHighlighted, bool isDown) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool isHighlighted, bool isDown) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool isDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;

    void drawPopupMenuItem (juce::Graphics&,
                            const juce::Rectangle<int>&,
                            bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked,
                            bool hasSubMenu,
                            const juce::String& text,
                            const juce::String& shortcutText,
                            const juce::Drawable* icon,
                            const juce::Colour* textColour) override;

    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline    (juce::Graphics&, int w, int h, juce::TextEditor&) override;
};
