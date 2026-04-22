// JUCE application entry point for the Hardware Profiler standalone app.
// Creates the top-level DocumentWindow, sets MainComponent as its content, and starts the event loop.

#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"

class HardwareProfilerApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "Hardware Profiler"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

    // Outer bezel that gives the app the physical body of an old oscilloscope or CRT monitor.
    // Wraps MainComponent with a fixed-width off-white plastic border and propagates
    // scale-button resize events up to the DocumentWindow automatically.
    struct BezelFrame : public juce::Component
    {
        static constexpr int kBezel = 14;   // border thickness in pixels

        BezelFrame()
        {
            addAndMakeVisible (inner);
            setSize (inner.getWidth()  + 2 * kBezel,
                     inner.getHeight() + 2 * kBezel);
        }

        void paint (juce::Graphics& g) override
        {
            // Warm off-white — aged plastic / oscilloscope body
            g.fillAll (juce::Colour (0xffe2ddd4));

            // Subtle inner shadow on the screen-facing edge for depth
            const auto inner_r = getLocalBounds().reduced (kBezel).toFloat();
            g.setColour (juce::Colour (0x33000000));
            g.drawRect (inner_r.expanded (1.0f), 1.0f);
        }

        void resized() override
        {
            inner.setBounds (kBezel, kBezel,
                             getWidth()  - 2 * kBezel,
                             getHeight() - 2 * kBezel);
        }

        // When MainComponent calls setSize() (scale buttons), propagate to the window.
        void childBoundsChanged (juce::Component*) override
        {
            setSize (inner.getWidth()  + 2 * kBezel,
                     inner.getHeight() + 2 * kBezel);
        }

        MainComponent inner;
    };

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name)
        : juce::DocumentWindow(name,
                               juce::Colour (0xffe2ddd4),   // match bezel colour
                               juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new BezelFrame(), true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(HardwareProfilerApplication)