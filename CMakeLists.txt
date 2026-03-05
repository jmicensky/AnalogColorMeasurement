cmake_minimum_required(VERSION 3.22)
project(HardwareProfiler VERSION 1.0.0)

# Add the JUCE directory. Adjust the path if your JUCE folder is located elsewhere.
add_subdirectory(JUCE)

# Create a standalone GUI application
juce_add_gui_app(HardwareProfiler
    PRODUCT_NAME "Hardware Profiler"
    VERSION "1.0.0")

# Add your source files here
target_sources(HardwareProfiler PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/AudioEngine.cpp
    Source/StimulusPlan.cpp
    Source/SessionWriter.cpp
)

# Link the required JUCE modules
target_link_libraries(HardwareProfiler PRIVATE
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_dsp
)

# Generate JuceHeader.h for #include <JuceHeader.h> support
juce_generate_juce_header(HardwareProfiler)

# Ensure C++20 is used (recommended for JUCE 8)
target_compile_features(HardwareProfiler PUBLIC cxx_std_20)

target_compile_definitions(HardwareProfiler PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)