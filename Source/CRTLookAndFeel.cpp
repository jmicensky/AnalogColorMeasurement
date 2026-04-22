#include "CRTLookAndFeel.h"

CRTLookAndFeel::CRTLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId,          juce::Colour (kColBgBase));
    setColour (juce::DocumentWindow::backgroundColourId,           juce::Colour (kColBgBase));

    setColour (juce::Label::textColourId,                          juce::Colour (kColGreenPrimary));
    setColour (juce::Label::backgroundColourId,                    juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId,                       juce::Colours::transparentBlack);

    setColour (juce::TextButton::buttonColourId,                   juce::Colour (kColBgButtonOff));
    setColour (juce::TextButton::buttonOnColourId,                 juce::Colour (kColBgButtonOn));
    setColour (juce::TextButton::textColourOffId,                  juce::Colour (kColGreenDim));
    setColour (juce::TextButton::textColourOnId,                   juce::Colour (kColGreenPrimary));

    setColour (juce::ToggleButton::textColourId,                   juce::Colour (kColGreenDim));
    setColour (juce::ToggleButton::tickColourId,                   juce::Colour (kColGreenPrimary));
    setColour (juce::ToggleButton::tickDisabledColourId,           juce::Colour (kColGreenSubtle));

    setColour (juce::ComboBox::backgroundColourId,                 juce::Colour (kColBgContainer));
    setColour (juce::ComboBox::textColourId,                       juce::Colour (kColGreenDim));
    setColour (juce::ComboBox::outlineColourId,                    juce::Colour (kColGreenDim));
    setColour (juce::ComboBox::buttonColourId,                     juce::Colour (kColBgContainer));
    setColour (juce::ComboBox::arrowColourId,                      juce::Colour (kColGreenDim));
    setColour (juce::ComboBox::focusedOutlineColourId,             juce::Colour (kColGreenPrimary));

    setColour (juce::TextEditor::backgroundColourId,               juce::Colour (kColBgContainer));
    setColour (juce::TextEditor::textColourId,                     juce::Colour (kColGreenPrimary));
    setColour (juce::TextEditor::outlineColourId,                  juce::Colour (kColGreenDim));
    setColour (juce::TextEditor::focusedOutlineColourId,           juce::Colour (kColGreenPrimary));
    setColour (juce::TextEditor::highlightColourId,                juce::Colour (kColBgButtonOn));
    setColour (juce::TextEditor::highlightedTextColourId,          juce::Colour (kColGreenPrimary));
    setColour (juce::CaretComponent::caretColourId,                juce::Colour (kColGreenPrimary));

    setColour (juce::ListBox::backgroundColourId,                  juce::Colour (kColBgBase));
    setColour (juce::ListBox::outlineColourId,                     juce::Colour (kColGreenDim));
    setColour (juce::ListBox::textColourId,                        juce::Colour (kColGreenDim));

    setColour (juce::PopupMenu::backgroundColourId,                juce::Colour (kColBgContainer));
    setColour (juce::PopupMenu::textColourId,                      juce::Colour (kColGreenDim));
    setColour (juce::PopupMenu::headerTextColourId,                juce::Colour (kColGreenPrimary));
    setColour (juce::PopupMenu::highlightedBackgroundColourId,     juce::Colour (kColBgButtonOn));
    setColour (juce::PopupMenu::highlightedTextColourId,           juce::Colour (kColGreenPrimary));

    setColour (juce::ScrollBar::thumbColourId,                     juce::Colour (kColGreenDim));
    setColour (juce::ScrollBar::trackColourId,                     juce::Colour (kColBgContainer));
}

//==============================================================================
juce::Font CRTLookAndFeel::monoFont (float height)
{
    return juce::Font (juce::FontOptions()
        .withName ("Courier New")
        .withHeight (height));
}

juce::Font CRTLookAndFeel::getTextButtonFont (juce::TextButton&, int height)
{
    return monoFont (juce::jlimit (10.0f, 15.0f, (float) height * 0.5f));
}

juce::Font CRTLookAndFeel::getComboBoxFont (juce::ComboBox& cb)
{
    return monoFont (juce::jmin (15.0f, (float) cb.getHeight() * 0.55f));
}

juce::Font CRTLookAndFeel::getLabelFont (juce::Label& label)
{
    // Preserve explicitly-set height but force monospace typeface.
    return monoFont (label.getFont().getHeight());
}

//==============================================================================
void CRTLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& btn,
                                            const juce::Colour&,
                                            bool isHighlighted, bool isDown)
{
    const auto bounds = btn.getLocalBounds().toFloat().reduced (0.5f);
    const bool isOn   = btn.getToggleState();

    if (isDown || isOn)
        g.setColour (juce::Colour (kColBgButtonOn));
    else if (isHighlighted)
        g.setColour (juce::Colour (0xff202020));
    else
        g.setColour (juce::Colour (kColBgButtonOff));

    g.fillRoundedRectangle (bounds, 3.0f);

    if (isOn)
        g.setColour (juce::Colour (kColGreenPrimary));
    else if (isHighlighted)
        g.setColour (juce::Colour (kColGreenDim));
    else
        g.setColour (juce::Colour (0x3333ff33));

    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
}

void CRTLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& btn,
                                      bool /*isHighlighted*/, bool /*isDown*/)
{
    g.setColour (juce::Colour (btn.getToggleState() ? kColGreenPrimary : kColGreenDim));
    g.setFont (getTextButtonFont (btn, btn.getHeight()));
    g.drawFittedText (btn.getButtonText(),
                      btn.getLocalBounds().reduced (4, 2),
                      juce::Justification::centred, 1);
}

void CRTLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& btn,
                                        bool isHighlighted, bool /*isDown*/)
{
    const bool  isOn    = btn.getToggleState();
    const float h       = (float) btn.getHeight();
    const float ledSize = std::round (h * 0.40f);
    const float ledX    = 4.0f;
    const float ledY    = (h - ledSize) * 0.5f;

    if (isOn)
    {
        // Outer glow
        g.setColour (juce::Colour (kColGreenSubtle));
        g.fillEllipse (ledX - 2.0f, ledY - 2.0f, ledSize + 4.0f, ledSize + 4.0f);
        // Core
        g.setColour (juce::Colour (kColGreenPrimary));
        g.fillEllipse (ledX, ledY, ledSize, ledSize);
    }
    else
    {
        g.setColour (juce::Colour (0xff1a1a1a));
        g.fillEllipse (ledX, ledY, ledSize, ledSize);
        g.setColour (juce::Colour (0x3333ff33));
        g.drawEllipse (ledX, ledY, ledSize, ledSize, 1.0f);
    }

    const float textX = ledX + ledSize + 8.0f;
    g.setColour (isOn ? juce::Colour (kColGreenPrimary)
                      : (isHighlighted ? juce::Colour (kColGreenDim)
                                       : juce::Colour (0x4433ff33)));
    g.setFont (monoFont (juce::jlimit (11.0f, 14.0f, h * 0.5f)));
    g.drawText (btn.getButtonText(),
                (int) textX, 0,
                btn.getWidth() - (int) textX - 2, btn.getHeight(),
                juce::Justification::centredLeft);
}

//==============================================================================
void CRTLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h,
                                    bool isDown,
                                    int /*buttonX*/, int /*buttonY*/,
                                    int /*buttonW*/, int /*buttonH*/,
                                    juce::ComboBox& cb)
{
    const bool focused  = cb.hasKeyboardFocus (false);
    const auto borderCol = (focused || isDown) ? juce::Colour (kColGreenPrimary)
                                               : juce::Colour (kColGreenDim);

    g.setColour (juce::Colour (kColBgContainer));
    g.fillRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 3.0f);

    g.setColour (borderCol);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 3.0f, 1.0f);

    // Down arrow
    const float arrowX  = (float) w - 18.0f;
    const float arrowCY = (float) h * 0.5f;
    const float arrowS  = 5.0f;
    juce::Path arrow;
    arrow.addTriangle (arrowX,                arrowCY - arrowS * 0.5f,
                       arrowX + arrowS,       arrowCY - arrowS * 0.5f,
                       arrowX + arrowS * 0.5f, arrowCY + arrowS * 0.5f);
    g.setColour (juce::Colour (kColGreenDim));
    g.fillPath (arrow);
}

//==============================================================================
void CRTLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.setColour (juce::Colour (kColBgContainer));
    g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, 4.0f);
    g.setColour (juce::Colour (kColGreenDim));
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 4.0f, 1.0f);
}

void CRTLookAndFeel::drawPopupMenuItem (juce::Graphics& g,
                                         const juce::Rectangle<int>& area,
                                         bool isSeparator,
                                         bool isActive, bool isHighlighted,
                                         bool isTicked, bool /*hasSubMenu*/,
                                         const juce::String& text,
                                         const juce::String& /*shortcutText*/,
                                         const juce::Drawable* /*icon*/,
                                         const juce::Colour* /*textColour*/)
{
    if (isSeparator)
    {
        g.setColour (juce::Colour (kColGreenSubtle));
        g.drawHorizontalLine (area.getCentreY(),
                              (float) area.getX() + 4.0f,
                              (float) area.getRight() - 4.0f);
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour (juce::Colour (kColBgButtonOn));
        g.fillRect (area);
    }

    if (isTicked)
    {
        const float sz = (float) area.getHeight() * 0.35f;
        g.setColour (juce::Colour (kColGreenPrimary));
        g.fillEllipse ((float) area.getX() + 4.0f,
                       (float) area.getCentreY() - sz * 0.5f, sz, sz);
    }

    const auto textCol = ! isActive
        ? juce::Colour (0x3333ff33)
        : (isHighlighted ? juce::Colour (kColGreenPrimary) : juce::Colour (kColGreenDim));

    g.setColour (textCol);
    g.setFont (monoFont (13.0f));
    g.drawFittedText (text,
                      area.withTrimmedLeft (isTicked ? 20 : 8).withTrimmedRight (4),
                      juce::Justification::centredLeft, 1);
}

//==============================================================================
void CRTLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h,
                                                juce::TextEditor&)
{
    g.setColour (juce::Colour (kColBgContainer));
    g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, 3.0f);
}

void CRTLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h,
                                             juce::TextEditor& ed)
{
    const bool focused = ed.hasKeyboardFocus (true);
    g.setColour (focused ? juce::Colour (kColGreenPrimary) : juce::Colour (kColGreenDim));
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 3.0f, 1.0f);
}
