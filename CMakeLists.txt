cmake_minimum_required(VERSION 3.22)
project(HardwareProfiler VERSION 1.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(JUCE)

# =============================================================================
# Standalone measurement app
# =============================================================================
juce_add_gui_app(HardwareProfiler
    PRODUCT_NAME "Hardware Profiler"
    VERSION "1.1.0"
    PLIST_TO_MERGE "<plist version=\"1.0\"><dict><key>NSMicrophoneUsageDescription</key><string>Hardware Profiler needs microphone access to record audio from your interface.</string></dict></plist>")

juce_add_binary_data(HardwareProfilerData SOURCES
    "Reference Files/DRUMS_testtone.wav"
    "Reference Files/BASSDI_testtone.wav"
    "Reference Files/ElecKeys_testtone.wav"
)

target_sources(HardwareProfiler PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/AudioEngine.cpp
    Source/LatencyAligner.cpp
    Source/StimulusPlan.cpp
    Source/SessionWriter.cpp
    Source/SignalGenerator.cpp
    Source/AnalysisEngine.cpp
    Source/ArtifactFile.cpp
    Source/SpectrumDisplay.cpp
    Source/LevelMeter.cpp
)

target_link_libraries(HardwareProfiler PRIVATE
    HardwareProfilerData
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

juce_generate_juce_header(HardwareProfiler)
target_compile_features(HardwareProfiler PUBLIC cxx_std_20)
target_compile_definitions(HardwareProfiler PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

# =============================================================================
# Plugin (VST3 + AU)
# =============================================================================
juce_add_plugin(HardwareColor
    PRODUCT_NAME     "Hardware Color"
    VERSION          "1.2.0"
    PLUGIN_MANUFACTURER_CODE Jmce
    PLUGIN_CODE      HdCl
    FORMATS          VST3 AU
    IS_SYNTH         FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT   FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES  "Fx|Distortion"
    AU_MAIN_TYPE     kAudioUnitType_Effect
)

target_sources(HardwareColor PRIVATE
    Plugin/PluginProcessor.cpp
    Plugin/PluginEditor.cpp
    Source/AnalysisEngine.cpp
    Source/ArtifactFile.cpp
    Source/SpectrumDisplay.cpp
)

target_include_directories(HardwareColor PRIVATE Source)

target_link_libraries(HardwareColor PRIVATE
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
)

juce_generate_juce_header(HardwareColor)
target_compile_features(HardwareColor PUBLIC cxx_std_20)
target_compile_definitions(HardwareColor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
