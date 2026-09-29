juce_add_console_app(A8ManagerRegressionTests PRODUCT_NAME "A8Manager Regression Tests")
juce_generate_juce_header(A8ManagerRegressionTests)

# Compile the actual implementation under test, not copies of its algorithms.
target_sources(A8ManagerRegressionTests PRIVATE
    RegressionTests.cpp
    ../Source/Assimil8or/Assimil8orPreset.cpp
    ../Source/Assimil8or/FileTypeHelpers.cpp
    ../Source/Assimil8or/Audio/AudioManager.cpp
    ../Source/Assimil8or/Preset/ChannelProperties.cpp
    ../Source/Assimil8or/Preset/ParameterPresetListProperties.cpp
    ../Source/Assimil8or/Preset/ParameterPresetsSingleton.cpp
    ../Source/Assimil8or/Preset/PresetHelpers.cpp
    ../Source/Assimil8or/Preset/PresetProperties.cpp
    ../Source/Assimil8or/Preset/ZoneProperties.cpp
    ../submodules/oolib/oolib/ValueTree/ValueTreeHelpers.cpp)

target_include_directories(A8ManagerRegressionTests PRIVATE
    ../Source
    ${OOLIB_INCLUDE_DIR})
target_compile_features(A8ManagerRegressionTests PRIVATE cxx_std_17)
target_compile_definitions(A8ManagerRegressionTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    DONT_SET_USING_JUCE_NAMESPACE=1)
target_link_libraries(A8ManagerRegressionTests PRIVATE
    A8ManagerBinaryData
    juce::juce_audio_formats
    juce::juce_data_structures
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

add_test(NAME ParserCvRegression COMMAND A8ManagerRegressionTests --parser-cv)
add_test(NAME StereoSplitRegression COMMAND A8ManagerRegressionTests --stereo-split)
add_test(NAME ZoneContinuationRegression COMMAND A8ManagerRegressionTests --zone-continuation)
set_tests_properties(ZoneContinuationRegression PROPERTIES TIMEOUT 60)
set_tests_properties(ParserCvRegression StereoSplitRegression PROPERTIES TIMEOUT 60)

juce_add_console_app(A8ManagerInteractionTests PRODUCT_NAME "A8Manager Interaction Tests")
juce_generate_juce_header(A8ManagerInteractionTests)
target_sources(A8ManagerInteractionTests PRIVATE ValueDragTests.cpp
    CvAuditionTests.cpp
    WaveformAuditionTests.cpp
    WaveformAuditionRoutingTests.cpp
    ../Source/Assimil8or/Audio/WaveformAudition.cpp
    WaveformDesignTests.cpp
    WaveformDesignExportTests.cpp
    WaveformDesignAssignmentTests.cpp
    SharedPresetSessionTests.cpp
    ChannelCvSafetyTests.cpp
    GeneratedSidecarValidatorTests.cpp
    WaveformDesignRecallTests.cpp
    WaveformWorkspaceTests.cpp
    WaveformDurationTests.cpp
    ../Source/Assimil8or/Audio/WaveformDesign.cpp
    ../Source/Assimil8or/Audio/WaveformDesignExport.cpp
    ../Source/Assimil8or/Audio/WaveformDesignAssignment.cpp
    ../Source/GUI/WaveformWorkspace.cpp
    RootFolderTests.cpp
    PlaybackTests.cpp
    LoopPointsTests.cpp
    ZoneVoltageTests.cpp
    RegionMoveTests.cpp
    WaveformWorkflowTests.cpp
    EditorRefinementTests.cpp
    AudioAuditTests.cpp
    AudioFileSafetyTests.cpp
    StereoAssignmentTests.cpp
    StereoChannelUiTests.cpp
    ChannelPurgeUiTests.cpp
    PairedZoneEditTests.cpp
    PresetWorkflowAuditTests.cpp
    ../Source/Assimil8or/Assimil8orPreset.cpp
    ../Source/Assimil8or/Assimil8orValidator.cpp
    ../Source/Assimil8or/Validator/ValidatorProperties.cpp
    ../Source/Assimil8or/Validator/ValidatorResultListProperties.cpp
    ../Source/Assimil8or/Validator/ValidatorResultProperties.cpp
    ../Source/Assimil8or/Fixer/FixerEntryProperties.cpp
    ../Source/Assimil8or/FileTypeHelpers.cpp
    ../Source/Assimil8or/MidiSetup/MidiSetupFile.cpp
    ../Source/Assimil8or/MidiSetup/MidiSetupProperties.cpp
    ../Source/GUI/Assimil8or/Editor/ZoneEditor.cpp
    ../Source/GUI/Assimil8or/Editor/ChannelEditor.cpp
    ../Source/GUI/Assimil8or/Editor/Assimil8orEditorComponent.cpp
    ../Source/GUI/Assimil8or/Editor/CvInputComboBox.cpp
    ../Source/GUI/GuiControlProperties.cpp
    ../Source/GUI/Assimil8or/Editor/EditManager.cpp
    ../Source/GUI/Assimil8or/Editor/FormatHelpers.cpp
    ../Source/GUI/Assimil8or/Editor/ParameterToolTipData.cpp
    ../Source/GUI/Assimil8or/Editor/Envelope/AREnvelopeComponent.cpp
    ../Source/GUI/Assimil8or/Editor/Envelope/AREnvelopeProperties.cpp
    ../Source/SystemServices.cpp
    ../Source/Assimil8or/Audio/AudioManager.cpp
    ../submodules/oolib/oolib/GUI/FileSelectLabel.cpp
    ../submodules/oolib/oolib/GUI/CustomComboBox.cpp
    ../submodules/oolib/oolib/GUI/CustomTextButton.cpp
    ../Source/GUI/Assimil8or/Editor/Waveform/WaveformDisplay.cpp
    ../submodules/oolib/oolib/GUI/WaveformView.cpp
    ../Source/GUI/Assimil8or/Editor/LoopPoints/LoopPointsView.cpp
    ../Source/Assimil8or/Audio/AudioPlayer.cpp
    ../Source/Assimil8or/Audio/AuditionStretch.cpp
    ../Source/Assimil8or/Audio/AudioPlayerProperties.cpp
    ../Source/Assimil8or/Audio/AudioSettingsProperties.cpp
    ../Source/Assimil8or/PresetManagerProperties.cpp
    ../Source/Assimil8or/Preset/ChannelProperties.cpp
    ../Source/Assimil8or/Preset/PresetProperties.cpp
    ../Source/Assimil8or/Preset/ZoneProperties.cpp
    ../Source/Assimil8or/Preset/PresetHelpers.cpp
    ../Source/Assimil8or/Preset/ParameterPresetsSingleton.cpp
    ../Source/Assimil8or/Preset/ParameterPresetListProperties.cpp
    ../Source/GUI/Assimil8or/Editor/SampleManager/SampleProperties.cpp
    ../Source/GUI/Assimil8or/Editor/SampleManager/SampleManager.cpp
    ../Source/GUI/Assimil8or/Editor/SampleManager/SampleManagerProperties.cpp
    ../Source/GUI/CurrentFolderComponent.cpp
    ../Source/AppProperties.cpp
    ../submodules/oolib/oolib/Directory/DirectoryDataProperties.cpp
    ../submodules/oolib/oolib/Properties/PersistentRootProperties.cpp
    ../submodules/oolib/oolib/Properties/RuntimeRootProperties.cpp
    ../submodules/oolib/oolib/ValueTree/ValueTreeHelpers.cpp
    ../submodules/oolib/oolib/GUI/CustomComponentMouseHandler.cpp
    ../submodules/oolib/oolib/GUI/ErrorHelpers.cpp)
target_include_directories(A8ManagerInteractionTests PRIVATE ../Source ${OOLIB_INCLUDE_DIR})
target_compile_features(A8ManagerInteractionTests PRIVATE cxx_std_17)
target_compile_definitions(A8ManagerInteractionTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=0 DONT_SET_USING_JUCE_NAMESPACE=1)
target_link_libraries(A8ManagerInteractionTests PRIVATE A8ManagerBinaryData signalsmith-stretch juce::juce_audio_utils juce::juce_data_structures juce::juce_dsp juce::juce_recommended_config_flags)
set_source_files_properties(../Source/Assimil8or/Audio/AuditionStretch.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2>")
add_test(NAME ValueDragRegression COMMAND A8ManagerInteractionTests)
add_test(NAME WaveformDesignRegression COMMAND A8ManagerInteractionTests --waveform-design)
add_test(NAME WaveformDesignExportRegression COMMAND A8ManagerInteractionTests --waveform-design-export)
add_test(NAME WaveformDesignAssignmentRegression COMMAND A8ManagerInteractionTests --waveform-design-assignment)
add_test(NAME SharedPresetSessionRegression COMMAND A8ManagerInteractionTests --shared-preset-session)
add_test(NAME ChannelCvSafetyRegression COMMAND A8ManagerInteractionTests --channel-cv-safety)
add_test(NAME GeneratedSidecarValidatorRegression COMMAND A8ManagerInteractionTests --generated-sidecar-validator)
add_test(NAME WaveformDesignRecallRegression COMMAND A8ManagerInteractionTests --waveform-design-recall)
set_tests_properties(WaveformDesignRecallRegression PROPERTIES TIMEOUT 90 FAIL_REGULAR_EXPRESSION "JUCE Assertion failure")
set_tests_properties(GeneratedSidecarValidatorRegression PROPERTIES TIMEOUT 90 FAIL_REGULAR_EXPRESSION "JUCE Assertion failure")
set_tests_properties(WaveformDesignAssignmentRegression SharedPresetSessionRegression ChannelCvSafetyRegression
    PROPERTIES TIMEOUT 90 FAIL_REGULAR_EXPRESSION "JUCE Assertion failure")
add_test(NAME WaveformWorkspaceRegression COMMAND A8ManagerInteractionTests --waveform-workspace)
add_test(NAME WaveformDurationRegression COMMAND A8ManagerInteractionTests --waveform-duration)
add_test(NAME WaveformAuditionRegression COMMAND A8ManagerInteractionTests --waveform-audition)
add_test(NAME WaveformAuditionRoutingRegression COMMAND A8ManagerInteractionTests --waveform-audition-routing)
add_test(NAME CvAuditionRegression COMMAND A8ManagerInteractionTests --cv-audition)
set_tests_properties(CvAuditionRegression PROPERTIES TIMEOUT 90 FAIL_REGULAR_EXPRESSION "JUCE Assertion failure")
set_tests_properties(WaveformAuditionRegression WaveformAuditionRoutingRegression
    PROPERTIES TIMEOUT 90 FAIL_REGULAR_EXPRESSION "JUCE Assertion failure")
set_tests_properties(WaveformDesignRegression WaveformDesignExportRegression WaveformWorkspaceRegression WaveformDurationRegression
    PROPERTIES TIMEOUT 90 FAIL_REGULAR_EXPRESSION "JUCE Assertion failure")
add_test(NAME RootFolderRegression COMMAND A8ManagerInteractionTests --root-folder)
add_test(NAME PlaybackRegression COMMAND A8ManagerInteractionTests --playback)
add_test(NAME LoopPointsRegression COMMAND A8ManagerInteractionTests --loop-preview)
add_test(NAME ZoneVoltageRegression COMMAND A8ManagerInteractionTests --zone-voltages)
add_test(NAME RegionMoveRegression COMMAND A8ManagerInteractionTests --region-move)
add_test(NAME WaveformWorkflowRegression COMMAND A8ManagerInteractionTests --waveform-workflow)
add_test(NAME EditorRefinementRegression COMMAND A8ManagerInteractionTests --editor-refinements)
add_test(NAME StereoPreviewRegression COMMAND A8ManagerInteractionTests --stereo-preview)
add_test(NAME AudioAuditRegression COMMAND A8ManagerInteractionTests --audio-audit)
add_test(NAME AudioFileSafetyRegression COMMAND A8ManagerInteractionTests --audio-file-safety)
add_test(NAME StereoAssignmentRegression COMMAND A8ManagerInteractionTests --stereo-assignment)
add_test(NAME StereoChannelUiRegression COMMAND A8ManagerInteractionTests --stereo-channel-ui)
add_test(NAME ChannelPurgeUiRegression COMMAND A8ManagerInteractionTests --channel-purge-ui)
set_tests_properties(ChannelPurgeUiRegression PROPERTIES TIMEOUT 90 FAIL_REGULAR_EXPRESSION "JUCE Assertion failure")
add_test(NAME PairedZoneEditRegression COMMAND A8ManagerInteractionTests --paired-zone-edit)
add_test(NAME PresetWorkflowAuditRegression COMMAND A8ManagerInteractionTests --preset-workflow-audit)
set_tests_properties(AudioAuditRegression AudioFileSafetyRegression StereoAssignmentRegression StereoChannelUiRegression PairedZoneEditRegression PresetWorkflowAuditRegression PROPERTIES TIMEOUT 60)
set_tests_properties(StereoChannelUiRegression PROPERTIES FAIL_REGULAR_EXPRESSION "JUCE Assertion failure")
set_tests_properties(EditorRefinementRegression StereoPreviewRegression PROPERTIES TIMEOUT 60)
set_tests_properties(ValueDragRegression RootFolderRegression PlaybackRegression LoopPointsRegression ZoneVoltageRegression RegionMoveRegression WaveformWorkflowRegression PROPERTIES TIMEOUT 60)
