cmake_minimum_required(VERSION 3.24)

project(A8Manager VERSION 3.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Header-only DSP libraries, pinned for reproducible pitch-preserving audition.
# Downloads are cached in the build tree, not added to the JUCE/oolib submodules.
include(FetchContent)
FetchContent_Declare(signalsmith-linear
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
    GIT_TAG de55e6a50ffcf6f8f43f649692d94691c7025151
    UPDATE_DISCONNECTED TRUE
    GIT_SUBMODULES "")
FetchContent_Declare(signalsmith-stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG a670068d9aeb64913331d5cc29337b19a457a7df
    UPDATE_DISCONNECTED TRUE
    GIT_SUBMODULES "")
FetchContent_MakeAvailable(signalsmith-linear signalsmith-stretch)

# JUCE is pinned to 9.0.2 (including the missing-final-WAV-padding fix).
# Submodules must be initialised before building:
#   git submodule update --init --recursive
add_subdirectory(submodules/JUCE builds/JUCE)

# The shared utility sources that used to live in Source/Utility. This defines
# OOLIB_SOURCES and OOLIB_INCLUDE_DIR, and is included rather than added as a
# subdirectory because the sources #include <JuceHeader.h> and so have to be
# compiled as part of a JUCE target.
include(submodules/oolib/oolib.cmake)

juce_add_gui_app(A8Manager
    PRODUCT_NAME    "A8Manager"
    COMPANY_NAME    "OmOhmProductions"
    COMPANY_WEBSITE "www.omohmproductions.com"
    ICON_BIG        "Source/GUI/Assimil8or/Data/377906243_984640136172516_2914152204379747274_n.png"
    ICON_SMALL      "Source/GUI/Assimil8or/Data/377906243_984640136172516_2914152204379747274_n.png")

# The sources include <JuceHeader.h> rather than the individual module headers,
# so the header the Projucer used to generate is still needed.
juce_generate_juce_header(A8Manager)

# Without this, Visual Studio opens with ALL_BUILD selected and F5 fails with
# "Unable to start program ... ALL_BUILD. Access is denied." - ALL_BUILD is a
# utility target with nothing to launch.
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT A8Manager)

set(SOURCE_FILES
    Source/AppProperties.cpp
    Source/AppProperties.h
    Source/Main.cpp
    Source/SystemServices.cpp
    Source/SystemServices.h)

set(ASSIMIL8OR_FILES
    Source/Assimil8or/Assimil8orPreset.cpp
    Source/Assimil8or/Assimil8orPreset.h
    Source/Assimil8or/Assimil8orValidator.cpp
    Source/Assimil8or/Assimil8orValidator.h
    Source/Assimil8or/FileTypeHelpers.cpp
    Source/Assimil8or/FileTypeHelpers.h
    Source/Assimil8or/SafeRename.h
    Source/Assimil8or/PresetManagerProperties.cpp
    Source/Assimil8or/PresetManagerProperties.h)

set(ASSIMIL8OR_AUDIO_FILES
    Source/Assimil8or/Audio/HardwareTestOutput.cpp
    Source/Assimil8or/Audio/HardwareTestOutput.h
    Source/Assimil8or/Audio/WaveformAudition.cpp
    Source/Assimil8or/Audio/WaveformAudition.h
    Source/Assimil8or/Audio/CvSampleSafety.h
    Source/Assimil8or/Audio/ChannelCvSafety.h
    Source/Assimil8or/Audio/WaveformDesign.cpp
    Source/Assimil8or/Audio/WaveformDesign.h
    Source/Assimil8or/Audio/WaveformDesignExport.cpp
    Source/Assimil8or/Audio/WaveformDesignExport.h
    Source/Assimil8or/Audio/WaveformDesignAssignment.cpp
    Source/Assimil8or/Audio/WaveformDesignAssignment.h
    Source/Assimil8or/Audio/WaveformDesignSidecars.h
    Source/Assimil8or/Audio/WaveformDesignRecall.h
    Source/Assimil8or/Audio/AudioManager.cpp
    Source/Assimil8or/Audio/AudioManager.h
    Source/Assimil8or/Audio/AudioPlayer.cpp
    Source/Assimil8or/Audio/AudioPlayer.h
    Source/Assimil8or/Audio/AuditionStretch.cpp
    Source/Assimil8or/Audio/AuditionStretch.h
    Source/Assimil8or/Audio/AudioPlayerProperties.cpp
    Source/Assimil8or/Audio/AudioPlayerProperties.h
    Source/Assimil8or/Audio/AudioSettingsProperties.cpp
    Source/Assimil8or/Audio/AudioSettingsProperties.h)

set(ASSIMIL8OR_FIXER_FILES
    Source/Assimil8or/Fixer/FixerEntryProperties.cpp
    Source/Assimil8or/Fixer/FixerEntryProperties.h)

set(ASSIMIL8OR_MIDISETUP_FILES
    Source/Assimil8or/MidiSetup/MidiSetupFile.cpp
    Source/Assimil8or/MidiSetup/MidiSetupFile.h
    Source/Assimil8or/MidiSetup/MidiSetupProperties.cpp
    Source/Assimil8or/MidiSetup/MidiSetupProperties.h)

set(ASSIMIL8OR_PRESET_FILES
    Source/Assimil8or/Preset/ChannelProperties.cpp
    Source/Assimil8or/Preset/ChannelProperties.h
    Source/Assimil8or/Preset/ParameterHelpers.cpp
    Source/Assimil8or/Preset/ParameterHelpers.h
    Source/Assimil8or/Preset/ParameterNames.h
    Source/Assimil8or/Preset/ParameterPresetListProperties.cpp
    Source/Assimil8or/Preset/ParameterPresetListProperties.h
    Source/Assimil8or/Preset/ParameterPresetsSingleton.cpp
    Source/Assimil8or/Preset/ParameterPresetsSingleton.h
    Source/Assimil8or/Preset/PresetHelpers.cpp
    Source/Assimil8or/Preset/PresetHelpers.h
    Source/Assimil8or/Preset/PresetProperties.cpp
    Source/Assimil8or/Preset/PresetProperties.h
    Source/Assimil8or/Preset/StereoChannelTools.h
    Source/Assimil8or/Preset/ZoneProperties.cpp
    Source/Assimil8or/Preset/ZoneProperties.h
    Source/Assimil8or/Preset/ZonePurge.h)

set(ASSIMIL8OR_PRESET_DATA_FILES
    Source/Assimil8or/Preset/Data/DefaultPreset.xml
    Source/Assimil8or/Preset/Data/MaxPreset.xml
    Source/Assimil8or/Preset/Data/MinPreset.xml)

set(ASSIMIL8OR_VALIDATOR_FILES
    Source/Assimil8or/Validator/ValidatorProperties.cpp
    Source/Assimil8or/Validator/ValidatorProperties.h
    Source/Assimil8or/Validator/ValidatorResultListProperties.cpp
    Source/Assimil8or/Validator/ValidatorResultListProperties.h
    Source/Assimil8or/Validator/ValidatorResultProperties.cpp
    Source/Assimil8or/Validator/ValidatorResultProperties.h)

set(GUI_FILES
    Source/GUI/HardwareTestOutputComponent.cpp
    Source/GUI/HardwareTestOutputComponent.h
    Source/GUI/WaveformWorkspace.cpp
    Source/GUI/WaveformWorkspace.h
    Source/GUI/PresetEditSession.h
    Source/GUI/BottomStatusWindow.cpp
    Source/GUI/BottomStatusWindow.h
    Source/GUI/CurrentFolderComponent.cpp
    Source/GUI/CurrentFolderComponent.h
    Source/GUI/GuiControlProperties.cpp
    Source/GUI/GuiControlProperties.h
    Source/GUI/GuiProperties.cpp
    Source/GUI/GuiProperties.h
    Source/GUI/MainComponent.cpp
    Source/GUI/MainComponent.h)

set(GUI_ASSIMIL8OR_DATA_FILES
    Source/GUI/Assimil8or/Data/377906243_984640136172516_2914152204379747274_n.png
    Source/GUI/Assimil8or/Data/Assimil8orToolTips.xml)

set(GUI_EDITOR_FILES
    Source/GUI/Assimil8or/Editor/WaveformDuration.h
    Source/GUI/Assimil8or/Editor/Assimil8orEditorComponent.cpp
    Source/GUI/Assimil8or/Editor/Assimil8orEditorComponent.h
    Source/GUI/Assimil8or/Editor/ChannelEditor.cpp
    Source/GUI/Assimil8or/Editor/ChannelEditor.h
    Source/GUI/Assimil8or/Editor/ZoneVoltageDisplay.h
    Source/GUI/Assimil8or/Editor/CvInputComboBox.cpp
    Source/GUI/Assimil8or/Editor/CvInputComboBox.h
    Source/GUI/Assimil8or/Editor/EditManager.cpp
    Source/GUI/Assimil8or/Editor/EditManager.h
    Source/GUI/Assimil8or/Editor/FormatHelpers.cpp
    Source/GUI/Assimil8or/Editor/FormatHelpers.h
    Source/GUI/Assimil8or/Editor/MinVoltageTests.h
    Source/GUI/Assimil8or/Editor/ParameterToolTipData.cpp
    Source/GUI/Assimil8or/Editor/ParameterToolTipData.h
    Source/GUI/Assimil8or/Editor/ZoneEditor.cpp
    Source/GUI/Assimil8or/Editor/ZoneEditor.h)

set(GUI_EDITOR_ENVELOPE_FILES
    Source/GUI/Assimil8or/Editor/Envelope/AREnvelopeComponent.cpp
    Source/GUI/Assimil8or/Editor/Envelope/AREnvelopeComponent.h
    Source/GUI/Assimil8or/Editor/Envelope/AREnvelopeProperties.cpp
    Source/GUI/Assimil8or/Editor/Envelope/AREnvelopeProperties.h)

set(GUI_EDITOR_LOOPPOINTS_FILES
    Source/GUI/Assimil8or/Editor/LoopPoints/LoopPointsView.cpp
    Source/GUI/Assimil8or/Editor/LoopPoints/LoopPointsView.h)

set(GUI_EDITOR_SAMPLEMANAGER_FILES
    Source/GUI/Assimil8or/Editor/SampleManager/SampleManager.cpp
    Source/GUI/Assimil8or/Editor/SampleManager/SampleManager.h
    Source/GUI/Assimil8or/Editor/SampleManager/SampleManagerProperties.cpp
    Source/GUI/Assimil8or/Editor/SampleManager/SampleManagerProperties.h
    Source/GUI/Assimil8or/Editor/SampleManager/SampleProperties.cpp
    Source/GUI/Assimil8or/Editor/SampleManager/SampleProperties.h
    Source/GUI/Assimil8or/Editor/SampleManager/SampleStatus.h)

set(GUI_EDITOR_WAVEFORM_FILES
    Source/GUI/Assimil8or/Editor/Waveform/WaveformPresentation.h
    Source/GUI/Assimil8or/Editor/Waveform/WaveformRuler.h
    Source/GUI/Assimil8or/Editor/Waveform/WaveformMarkers.h
    Source/GUI/Assimil8or/Editor/Waveform/RegionMove.h
    Source/GUI/Assimil8or/Editor/Waveform/RegionMoveWaveform.h
    Source/GUI/Assimil8or/Editor/Waveform/WaveformDisplay.cpp
    Source/GUI/Assimil8or/Editor/Waveform/WaveformDisplay.h)

set(GUI_FILEVIEW_FILES
    Source/GUI/Assimil8or/FileView/FileViewComponent.cpp
    Source/GUI/Assimil8or/FileView/FileViewComponent.h)

set(GUI_MIDICONFIG_FILES
    Source/GUI/Assimil8or/MidiConfig/MidiConfigComponent.cpp
    Source/GUI/Assimil8or/MidiConfig/MidiConfigComponent.h
    Source/GUI/Assimil8or/MidiConfig/MidiConfigDialogComponent.cpp
    Source/GUI/Assimil8or/MidiConfig/MidiConfigDialogComponent.h
    Source/GUI/Assimil8or/MidiConfig/MidiSetupEditorComponent.cpp
    Source/GUI/Assimil8or/MidiConfig/MidiSetupEditorComponent.h)

set(GUI_PRESETLIST_FILES
    Source/GUI/Assimil8or/PresetList/PresetListComponent.cpp
    Source/GUI/Assimil8or/PresetList/PresetListComponent.h)

set(GUI_VALIDATOR_FILES
    Source/GUI/Assimil8or/Validator/Assimil8orValidatorComponent.cpp
    Source/GUI/Assimil8or/Validator/Assimil8orValidatorComponent.h
    Source/GUI/Assimil8or/Validator/LocateFileComponent.cpp
    Source/GUI/Assimil8or/Validator/LocateFileComponent.h
    Source/GUI/Assimil8or/Validator/RenameDialogComponent.cpp
    Source/GUI/Assimil8or/Validator/RenameDialogComponent.h
    Source/GUI/Assimil8or/Validator/ValidatorComponentProperties.cpp
    Source/GUI/Assimil8or/Validator/ValidatorComponentProperties.h
    Source/GUI/Assimil8or/Validator/ValidatorToolWindow.cpp
    Source/GUI/Assimil8or/Validator/ValidatorToolWindow.h)

# The files the Projucer marked resource="1". They are built into a separate
# binary data target, but stay listed in the groups above so the IDE tree still
# shows them next to the code they belong with.
set(RESOURCE_FILES
    ${ASSIMIL8OR_PRESET_DATA_FILES}
    ${GUI_ASSIMIL8OR_DATA_FILES})

set_source_files_properties(${RESOURCE_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

juce_add_binary_data(A8ManagerBinaryData SOURCES ${RESOURCE_FILES})

source_group("Source"                                     FILES ${SOURCE_FILES})
source_group("Source/Assimil8or"                          FILES ${ASSIMIL8OR_FILES})
source_group("Source/Assimil8or/Audio"                    FILES ${ASSIMIL8OR_AUDIO_FILES})
source_group("Source/Assimil8or/Fixer"                    FILES ${ASSIMIL8OR_FIXER_FILES})
source_group("Source/Assimil8or/MidiSetup"                FILES ${ASSIMIL8OR_MIDISETUP_FILES})
source_group("Source/Assimil8or/Preset"                   FILES ${ASSIMIL8OR_PRESET_FILES})
source_group("Source/Assimil8or/Preset/Data"              FILES ${ASSIMIL8OR_PRESET_DATA_FILES})
source_group("Source/Assimil8or/Validator"                FILES ${ASSIMIL8OR_VALIDATOR_FILES})
source_group("Source/GUI"                                 FILES ${GUI_FILES})
source_group("Source/GUI/Assimil8or/Data"                 FILES ${GUI_ASSIMIL8OR_DATA_FILES})
source_group("Source/GUI/Assimil8or/Editor"               FILES ${GUI_EDITOR_FILES})
source_group("Source/GUI/Assimil8or/Editor/Envelope"      FILES ${GUI_EDITOR_ENVELOPE_FILES})
source_group("Source/GUI/Assimil8or/Editor/LoopPoints"    FILES ${GUI_EDITOR_LOOPPOINTS_FILES})
source_group("Source/GUI/Assimil8or/Editor/SampleManager" FILES ${GUI_EDITOR_SAMPLEMANAGER_FILES})
source_group("Source/GUI/Assimil8or/Editor/Waveform"      FILES ${GUI_EDITOR_WAVEFORM_FILES})
source_group("Source/GUI/Assimil8or/FileView"             FILES ${GUI_FILEVIEW_FILES})
source_group("Source/GUI/Assimil8or/MidiConfig"           FILES ${GUI_MIDICONFIG_FILES})
source_group("Source/GUI/Assimil8or/PresetList"           FILES ${GUI_PRESETLIST_FILES})
source_group("Source/GUI/Assimil8or/Validator"            FILES ${GUI_VALIDATOR_FILES})

# The oolib sources live outside the project tree, so group them by their own
# folder layout rather than letting the IDE scatter them.
source_group(TREE ${OOLIB_INCLUDE_DIR} PREFIX "oolib" FILES ${OOLIB_SOURCES})

target_sources(A8Manager
    PRIVATE
        ${SOURCE_FILES}
        ${ASSIMIL8OR_FILES}
        ${ASSIMIL8OR_AUDIO_FILES}
        ${ASSIMIL8OR_FIXER_FILES}
        ${ASSIMIL8OR_MIDISETUP_FILES}
        ${ASSIMIL8OR_PRESET_FILES}
        ${ASSIMIL8OR_PRESET_DATA_FILES}
        ${ASSIMIL8OR_VALIDATOR_FILES}
        ${GUI_FILES}
        ${GUI_ASSIMIL8OR_DATA_FILES}
        ${GUI_EDITOR_FILES}
        ${GUI_EDITOR_ENVELOPE_FILES}
        ${GUI_EDITOR_LOOPPOINTS_FILES}
        ${GUI_EDITOR_SAMPLEMANAGER_FILES}
        ${GUI_EDITOR_WAVEFORM_FILES}
        ${GUI_FILEVIEW_FILES}
        ${GUI_MIDICONFIG_FILES}
        ${GUI_PRESETLIST_FILES}
        ${GUI_VALIDATOR_FILES}
        ${OOLIB_SOURCES})

# oolib headers are included as "oolib/<Category>/<File>.h", so the search path is
# the directory that holds the oolib/ folder
target_include_directories(A8Manager
    PRIVATE
        ${OOLIB_INCLUDE_DIR})

# Keep the pre-9.0.2 software MP3-reader configuration. Native platform readers
# remain available; this upgrade must not silently change the import formats.
target_compile_definitions(A8Manager
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        DONT_SET_USING_JUCE_NAMESPACE=1)

target_link_libraries(A8Manager
    PRIVATE
        signalsmith-stretch
        A8ManagerBinaryData
        juce::juce_audio_processors_headless
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optimise spectral DSP in Clang/GCC Debug builds, without changing other code.
# MSVC Debug retains its runtime checks; use Release for listening on Windows.
set_source_files_properties(Source/Assimil8or/Audio/AuditionStretch.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2>")

# Carry the MIT notices with distributed builds, including the macOS app bundle.
# DaisySP is a pinned, vendored PolyBLEP excerpt; no new configure-time download.
set(daisysp_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Source/ThirdParty/DaisySP")
foreach(dependency IN ITEMS signalsmith-stretch signalsmith-linear daisysp)
    configure_file("${${dependency}_SOURCE_DIR}/LICENSE.txt"
                   "${CMAKE_CURRENT_BINARY_DIR}/licenses/${dependency}-LICENSE.txt" COPYONLY)
    if(APPLE)
        target_sources(A8Manager PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/licenses/${dependency}-LICENSE.txt")
        set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/licenses/${dependency}-LICENSE.txt"
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses")
    else()
        add_custom_command(TARGET A8Manager POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_BINARY_DIR}/licenses" "$<TARGET_FILE_DIR:A8Manager>/licenses")
    endif()
endforeach()

option(A8MANAGER_BUILD_TESTS "Build the application regression tests" OFF)
if(A8MANAGER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
