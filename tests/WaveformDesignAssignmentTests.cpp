#include "Assimil8or/Audio/WaveformDesignAssignment.h"
#include "Assimil8or/Audio/WaveformDesignExport.h"
#include "Assimil8or/Audio/AudioManager.h"
#include "Assimil8or/Audio/CvSampleSafety.h"
#include "Assimil8or/Assimil8orPreset.h"
#include "Assimil8or/Preset/ParameterPresetsSingleton.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void require (bool condition, const char* message) { if (! condition) throw std::runtime_error (message); }

    juce::ValueTree defaults ()
    {
        return ParameterPresetsSingleton::getInstance ()->getParameterPresetListProperties ()
            .getParameterPreset (ParameterPresetListProperties::DefaultParameterPresetType).createCopy ();
    }

    ZoneProperties zone (juce::ValueTree tree, int channel, int index = 0)
    {
        return ZoneProperties (tree.getChild (channel).getChild (index), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    }

    ChannelProperties channel (juce::ValueTree tree, int index)
    {
        return ChannelProperties (tree.getChild (index), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
    }

    bool empty (const WaveformDesign::AssignmentResult& result)
    {
        return ! result.editedPreset.isValid () && result.createdFiles.isEmpty () && result.waves.isEmpty () && result.recipe == juce::File () && result.ownership.empty ();
    }

    void verifyFiles (const WaveformDesign::AssignmentResult& result, const juce::File& folder, bool cv, int count)
    {
        require (result.waves.size () == count && result.createdFiles.size () == count + 1 && result.recipe.existsAsFile (), "Assignment publishes all voices and a unique recipe");
        AudioManager audio;
        for (const auto& file : result.waves)
        {
            require (file.getParentDirectory () == folder && file.getFileName ().length () <= 47, "Assigned WAVs have compatible flat names in the preset folder");
            const auto reader { audio.getReaderFor (file) };
            require (reader && reader->numChannels == 1 && reader->bitsPerSample == 24 && CvSampleSafety::isCv (file, *reader) == cv,
                     "Assignment preserves PCM24 format and explicit audio/CV purpose tags");
        }
        WaveformDesign::Settings restored;
        require (WaveformDesign::fromJson (juce::JSON::parse (result.recipe), restored).wasOk (), "Unique assignment recipe can be reopened");
    }
}

void testWaveformDesignAssignment ()
{
    using namespace WaveformDesign;
    const auto root { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-design-assignment", "", false) };
    require (root.createDirectory ().wasOk (), "Create owned assignment test folder");
    struct Cleanup { juce::File root; ~Cleanup () { root.deleteRecursively (); } } cleanup { root };
    const auto folder { root.getChildFile ("preset-folder") };
    require (folder.createDirectory ().wasOk (), "Create preset fixture folder");
    const auto existingPreset { folder.getChildFile ("prst091.yml") };
    require (existingPreset.replaceWithText ("Existing saved preset remains untouched\n"), "Create saved-preset preservation fixture");
    juce::MemoryBlock savedPresetBytes;
    require (existingPreset.loadFileAsData (savedPresetBytes), "Snapshot the saved preset bytes before assignment");
    const auto savedPresetIdentity { existingPreset.getFileIdentifier () };

    auto audio { startingPoint (Mode::oscillator, Shape::sine) };
    audio.cycleFrames = 128;
    auto cv { startingPoint (Mode::modulation, Shape::sine) };
    cv.durationSeconds = 0.01;
    auto source { defaults () };
    PresetProperties preset (source, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
    preset.setId (91, false);
    preset.setName ("Shared session", false);
    channel (source, 7).setPitch (7.5, false);
    channel (source, 3).setBits (18.0, false);
    channel (source, 3).setMixMod ("1A", 1.0, false);
    const auto untouched { source.createCopy () };

    const auto reservedId { reserveAssignmentId () };
    require (isAssignmentIdValid (reservedId) && reserveAssignmentId () != reservedId,
             "Each reserved assignment identifier contains twelve real lowercase hexadecimal characters");
    require (assignmentWaveName ("adf", reservedId, 1) == "adf-" + reservedId + "-01.wav"
             && assignmentWaveName ("Name with spaces", reservedId, 8) == "Name-with-spaces-" + reservedId + "-08.wav"
             && assignmentWaveName ("This name is longer than twenty two characters", reservedId, 1).length () <= 47
             && assignmentWaveName ("adf", reservedId, 0).isEmpty () && assignmentWaveName ("adf", reservedId, 9).isEmpty (),
             "Shared assignment naming preserves existing format and validates the one-based voice number");
    for (const auto& invalidId : { juce::String (), juce::String ("abcdefghijkl"), juce::String ("ABCDEF012345"),
                                  juce::String ("abc123"), juce::String ("../abcdefghi"), juce::String::repeatedString ("a", 10000) })
    {
        const auto countBefore { folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) };
        AssignmentResult invalid;
        require (! isAssignmentIdValid (invalidId) && assignmentFileStem ("adf", invalidId).isEmpty ()
                 && assignmentWaveName ("adf", invalidId, 1).isEmpty ()
                 && prepareAssignment (audio, folder, "adf", source, 0, 0, invalid, invalidId).failed () && empty (invalid)
                 && source.isEquivalentTo (untouched) && folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == countBefore,
                 "Invalid reserved identifiers are rejected before staging, preset changes or file publication");
    }
    AssignmentResult reserved;
    require (prepareAssignment (audio, folder, "adf", source, 0, 0, reserved, reservedId).wasOk ()
             && reserved.waves[0].getFileName () == assignmentWaveName ("adf", reservedId, 1)
             && reserved.recipe.getFileName () == assignmentFileStem ("adf", reservedId) + ".design.json",
             "The worker publishes exactly the reserved WAV and recipe names shown before generation");
    const auto reservedFileCount { folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) };
    juce::MemoryBlock reservedBytes;
    require (reserved.waves[0].loadFileAsData (reservedBytes), "Snapshot reserved output for no-overwrite regression");
    AssignmentResult collision;
    require (prepareAssignment (audio, folder, "adf", source, 0, 0, collision, reservedId).failed () && empty (collision)
             && folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == reservedFileCount,
             "Accidental reserved identifier reuse fails without replacing files or leaving a staging directory");
    juce::MemoryBlock afterCollision;
    require (reserved.waves[0].loadFileAsData (afterCollision) && afterCollision == reservedBytes
             && reserved.recipe.existsAsFile () && cleanupAssignmentFiles (reserved).wasOk (),
             "An identifier collision preserves previously published audio and its recipe byte-for-byte");

    AssignmentResult assigned;
    require (prepareAssignment (cv, folder, "My CV", source, 3, 0, assigned).wasOk (), "Assign CV into an empty independent channel");
    verifyFiles (assigned, folder, true, 1);
    require (assigned.waves[0].getFileName ().startsWith ("My-CV-") && assigned.waves[0].getFileName ().endsWith ("-01.wav")
             && assigned.recipe.getFileName ().startsWith ("My-CV-"), "Assigned WAV and recipe names start with the supplied name, retaining unique token and voice number");
    require (juce::JSON::parse (assigned.recipe)["displayName"].toString () == "My CV", "Assignment recipe retains the original readable design name");
    PresetProperties edited (assigned.editedPreset, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
    require (source.isEquivalentTo (untouched) && edited.getId () == 91 && edited.getName () == "Shared session", "Assignment never mutates its input or resets preset identity/name");
    require (channel (assigned.editedPreset, 3).getMixLevel () == -90.0 && std::get<0> (channel (assigned.editedPreset, 3).getMixMod ()) == "Off" &&
             channel (assigned.editedPreset, 3).getBits () == 18.0,
             "Generated CV is Mix Off while unrelated target-channel settings survive");
    require (zone (assigned.editedPreset, 3).getSample () == assigned.waves[0].getFileName () && zone (assigned.editedPreset, 3).getMinVoltage () == -5.0,
             "Generated voice is assigned to the requested channel's full-range first zone");
    for (int index { 0 }; index < 8; ++index)
        if (index != 3) require (assigned.editedPreset.getChild (index).isEquivalentTo (source.getChild (index)), "Unrelated channels are byte-for-property preserved");
    for (int index { 1 }; index < 8; ++index)
        require (assigned.editedPreset.getChild (3).getChild (index).isEquivalentTo (source.getChild (3).getChild (index)), "Other target-channel zones are preserved");
    juce::MemoryBlock afterPresetBytes;
    require (existingPreset.loadFileAsData (afterPresetBytes) && afterPresetBytes == savedPresetBytes &&
             existingPreset.getFileIdentifier () == savedPresetIdentity && folder.getNumberOfChildFiles (juce::File::findFiles, "*.yml") == 1,
             "Assignment never writes or replaces a saved preset file");
    const auto cvTree { assigned.editedPreset.createCopy () };
    AssignmentResult secondCv;
    require (prepareAssignment (cv, folder, "My CV", cvTree, 3, 1, secondCv).wasOk (), "CV may append into the next zone of a CV channel");
    require (secondCv.recipe != assigned.recipe && secondCv.waves[0] != assigned.waves[0], "Repeated assignments never overwrite earlier generated filenames");
    require (zone (secondCv.editedPreset, 3, 0).getMinVoltage () == 0.0 && zone (secondCv.editedPreset, 3, 1).getMinVoltage () == -5.0,
             "Append splits the previous zone's voltage range instead of creating an unreachable duplicate boundary");
    auto priorWithoutVoltage { secondCv.editedPreset.getChild (3).getChild (0).createCopy () };
    priorWithoutVoltage.setProperty (ZoneProperties::MinVoltagePropertyId, -5.0, nullptr);
    require (priorWithoutVoltage.isEquivalentTo (cvTree.getChild (3).getChild (0)), "Voltage split preserves the previous zone's sample/markers/offsets");

    auto reject = [&] (const Settings& settings, juce::ValueTree tree, int first, int target, const char* message)
    {
        const auto before { folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) };
        const auto snapshot { tree.createCopy () };
        AssignmentResult result;
        require (prepareAssignment (settings, folder, "Rejected", tree, first, target, result).failed () && empty (result) && tree.isEquivalentTo (snapshot) &&
                 folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == before, message);
    };
    reject (audio, cvTree, 3, 0, "Audio cannot replace the last CV zone and silently change channel purpose");
    reject (audio, cvTree, 3, 1, "Audio cannot be appended into an existing CV channel");
    reject (cv, source, 3, 2, "Assignment cannot create a sparse zone gap");
    reject (audio, source, -1, 0, "Invalid negative channel rejected");
    reject (audio, source, 0, 8, "Invalid zone rejected");
    reject (audio, {}, 0, 0, "Invalid preset structure rejected");

    AssignmentResult audioResult;
    require (prepareAssignment (audio, folder, "Audio", source, 1, 0, audioResult).wasOk (), "Assign ordinary audio into another independent channel");
    verifyFiles (audioResult, folder, false, 1);
    reject (cv, audioResult.editedPreset, 1, 0, "CV cannot replace an audio zone and silently repurpose its channel");
    reject (cv, audioResult.editedPreset, 1, 1, "CV cannot be mixed with an existing audio zone");
    auto mixed { audioResult.editedPreset.createCopy () };
    zone (mixed, 1, 0).setMinVoltage (0.0, false);
    zone (mixed, 1, 1).setSample (assigned.waves[0].getFileName (), false);
    zone (mixed, 1, 1).setMinVoltage (-5.0, false);
    reject (audio, mixed, 1, 0, "A pre-existing mixed-purpose channel is rejected even when target itself is audio");
    reject (cv, mixed, 1, 1, "A pre-existing mixed-purpose channel is rejected even when target itself is CV");

    auto missing { source.createCopy () };
    zone (missing, 1).setSample ("missing.wav", false);
    reject (audio, missing, 1, 0, "Unreadable existing references fail closed even on replacement");
    zone (missing, 1).setSample ("../outside.wav", false);
    reject (audio, missing, 1, 0, "Non-flat external references fail closed");
    auto sparse { audioResult.editedPreset.createCopy () };
    zone (sparse, 1, 2).setSample (audioResult.waves[0].getFileName (), false);
    reject (audio, sparse, 1, 1, "Pre-existing sparse used-zone layouts are rejected");

    auto stereo { source.createCopy () };
    channel (stereo, 2).setChannelMode (ChannelProperties::stereoRight, false);
    reject (audio, stereo, 1, 0, "Stereo-left target is rejected");
    reject (audio, stereo, 2, 0, "Stereo-right target is rejected");

    // Reported workflow: an occupied CH 1/2 pair must not reserve CH 3 or
    // redirect the designer's suggested first free destination to either side.
    const auto stereoFile { folder.getChildFile ("existing-stereo.wav") };
    {
        std::unique_ptr<juce::OutputStream> output { stereoFile.createOutputStream () };
        juce::WavAudioFormat format;
        auto writer { format.createWriterFor (output, juce::AudioFormatWriterOptions {}.withSampleRate (48000).withNumChannels (2).withBitsPerSample (24)) };
        juce::AudioBuffer<float> samples (2, 128);
        for (int frame { 0 }; frame < 128; ++frame)
        {
            samples.setSample (0, frame, 0.25f);
            samples.setSample (1, frame, -0.5f);
        }
        require (writer && writer->writeFromAudioSampleBuffer (samples, 0, 128) && writer->flush (), "Create real stereo fixture for designer CH 3 assignment");
    }
    auto occupiedStereo { source.createCopy () };
    channel (occupiedStereo, 1).setChannelMode (ChannelProperties::stereoRight, false);
    for (int side { 0 }; side < 2; ++side)
    {
        auto pairedZone { zone (occupiedStereo, side) };
        pairedZone.setSample (stereoFile.getFileName (), false);
        pairedZone.setSide (side, false);
        pairedZone.setSampleStart (8, false); pairedZone.setSampleEnd (100, false);
        pairedZone.setLoopStart (16, false); pairedZone.setLoopLength (32.0, false);
        pairedZone.setMinVoltage (-5.0, false);
    }
    const auto stereoBefore { occupiedStereo.createCopy () };
    for (const auto& settings : { audio, cv })
    {
        AssignmentResult afterStereo;
        require (prepareAssignment (settings, folder, "After stereo", occupiedStereo, 2, 0, afterStereo).wasOk (),
                 "Designer can assign audio or CV to CH 3 directly after an occupied CH 1/2 stereo pair");
        verifyFiles (afterStereo, folder, settings.mode == Mode::modulation, 1);
        require (zone (afterStereo.editedPreset, 2).getSample () == afterStereo.waves[0].getFileName () &&
                 channel (afterStereo.editedPreset, 2).getChannelMode () == ChannelProperties::master,
                 "Zero-based target 2 populates CH 3 as an independent channel");
        require (occupiedStereo.isEquivalentTo (stereoBefore) &&
                 afterStereo.editedPreset.getChild (0).isEquivalentTo (stereoBefore.getChild (0)) &&
                 afterStereo.editedPreset.getChild (1).isEquivalentTo (stereoBefore.getChild (1)),
                 "Generating for CH 3 preserves both existing stereo sides and the detached input");
        require (cleanupAssignmentFiles (afterStereo).wasOk (), "Remove owned un-applied stereo-neighbour assignment fixtures");
    }
    auto linked { source.createCopy () };
    channel (linked, 2).setChannelMode (ChannelProperties::link, false);
    reject (audio, linked, 1, 0, "A linked follower outside the assignment is protected");
    reject (audio, linked, 2, 0, "A linked leader outside the assignment is protected");

    auto bank { startingPoint (Mode::layers, Shape::saw) };
    bank.cycleFrames = 128;
    spreadVoices (bank, 3, 9.0, 120.0, 1.0);
    reject (bank, source, 6, 0, "Bank cannot overflow channel 8");
    AssignmentResult bankResult;
    require (prepareAssignment (bank, folder, "Bank", linked, 1, 0, bankResult).wasOk (), "A bank may replace an entirely covered link group");
    verifyFiles (bankResult, folder, false, 3);
    for (int index { 0 }; index < 3; ++index)
    {
        const auto voice { bank.voices[static_cast<size_t> (index)] };
        auto assignedChannel { channel (bankResult.editedPreset, index + 1) };
        require (assignedChannel.getChannelMode () == (index == 0 ? ChannelProperties::master : ChannelProperties::link) &&
                 assignedChannel.getPitch () == voice.detuneCents / 100.0 && assignedChannel.getPan () == voice.pan &&
                 zone (bankResult.editedPreset, index + 1).getSample () == bankResult.waves[index].getFileName (),
                 "Bank voices map to contiguous Master/Link channels with independent detune/pan and matching WAV references");
    }
    require (bankResult.editedPreset.getChild (0).isEquivalentTo (source.getChild (0)) &&
             bankResult.editedPreset.getChild (4).isEquivalentTo (source.getChild (4)), "Bank preserves channels outside its span");

    AssignmentResult replacement;
    require (prepareAssignment (cv, folder, "Replace", secondCv.editedPreset, 3, 0, replacement).wasOk () &&
             zone (replacement.editedPreset, 3, 0).getMinVoltage () == 0.0 &&
             replacement.editedPreset.getChild (3).getChild (1).isEquivalentTo (secondCv.editedPreset.getChild (3).getChild (1)),
             "Same-purpose replacement preserves voltage threshold and all other zones");
    const auto recipeToKeep { replacement.recipe };
    require (recipeToKeep.replaceWithText ("User changed this file before stale-result cleanup"), "Create changed-output rollback fixture");
    require (cleanupAssignmentFiles (replacement).failed () && recipeToKeep.existsAsFile () && replacement.waves.isEmpty (),
             "Rollback preserves a subsequently edited output and reports it while removing untouched generated siblings");
    require (cleanupAssignmentFiles (replacement).wasOk (), "Rollback can be called again safely");
    for (auto* result : { &assigned, &secondCv, &audioResult, &bankResult })
    {
        const auto files { result->createdFiles };
        require (cleanupAssignmentFiles (*result).wasOk () && empty (*result), "Stale/failed apply rolls back only this assignment's owned files");
        for (const auto& file : files) require (! file.exists (), "Rolled-back generated file removed");
    }
    require (existingPreset.existsAsFile () && recipeToKeep.existsAsFile (), "Rollback never removes the existing preset or preserved user edits");
    require (prepareAssignment (audio, existingPreset, "No folder", source, 0, 0, replacement).failed () && empty (replacement), "I/O destination errors publish no partial assignment");

    const auto staged { root.getChildFile ("stage-file") }, occupied { root.getChildFile ("occupied-file") };
    require (staged.replaceWithText ("new") && occupied.replaceWithText ("old") && ExportSupport::publishExclusive (staged, occupied).failed () &&
             occupied.loadFileAsString () == "old" && staged.loadFileAsString () == "new", "Exclusive flat-file publication cannot overwrite a concurrent collision");

    ExportResult exported;
    require (exportDesign (audio, root, "Selected slot", exported, 73).wasOk () && exported.preset.getFileName () == "prst073.yml",
             "Standalone export accepts the selected preset slot");
    juce::StringArray lines;
    exported.preset.readLines (lines);
    Assimil8orPreset parser;
    parser.parse (lines);
    PresetProperties exportedPreset (parser.getPresetVT (), PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
    const auto notes { exported.folder.getChildFile ("README.txt").loadFileAsString () };
    require (parser.getParseErrorsVT ().getNumChildren () == 0 && exportedPreset.getId () == 73 && notes.contains ("preset 073") && notes.contains ("prst073.yml"),
             "Requested standalone slot survives preset parsing and export instructions");
    const auto countBefore { root.getNumberOfChildFiles (juce::File::findFilesAndDirectories) };
    require (exportDesign (audio, root, "Invalid slot", exported, 0).failed () && exportDesign (audio, root, "Invalid slot", exported, 200).failed () &&
             exported.folder == juce::File () && root.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == countBefore,
             "Out-of-range standalone slots fail without output");
    for (const auto& child : folder.findChildFiles (juce::File::findFilesAndDirectories, false))
        require (! child.getFileName ().startsWith (".a8-assignment-"), "No private assignment staging directory remains");
    std::cout << "PASS: detached design assignment, CV/audio separation, voltage append, linked/stereo safety, rollback, unique files and standalone preset slots\n";
}
