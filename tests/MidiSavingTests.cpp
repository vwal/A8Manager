#include "Assimil8or/MidiSetup/MidiSetupFile.h"
#include "GUI/Assimil8or/MidiConfig/MidiConfigDialogComponent.h"
#include "oolib/Properties/PersistentRootProperties.h"
#include "oolib/Properties/RuntimeRootProperties.h"
#include <stdexcept>

namespace
{
    void check (bool condition, const char* message)
    {
        if (! condition) throw std::runtime_error (message);
    }

    void testDocuments (const juce::File& folder)
    {
        const auto file { folder.getChildFile ("midi1.yml") };
        const juce::String text { "# hardware settings\r\n  # indented comment\r\nmode : 2\r\nassign : 14\r\nbasicchannel : 15\r\n"
                                 "rcvprogamchange : 2\r\nxmtprogamchange : 0\r\ncolAcc : 118\r\ncolBcc : 100\r\ncolCcc : -1\r\n"
                                 "pitchwheelsemi : 48  # maximum bend\r\nvelocitydepth : 127\r\nnotifications : 0\r\nindexbasekey : 119\r\n"
                                 "future_scalar: 'retain # this: exactly'\r\nfuture_array: [1, 2, 3]\r\n# no final newline" };
        check (file.replaceWithText (text, false, false, nullptr), "Write MIDI preservation fixture");
        MidiSetupFile document;
        check (document.read (file).wasOk (), "Read all current fields plus unknown flat fields");
        const auto baseline { document.getMidiSetupPropertiesVT ().createCopy () };
        MidiSetupProperties values (baseline, MidiSetupProperties::WrapperType::client, MidiSetupProperties::EnableCallbacks::no);
        check (values.getMode () == 2 && values.getAssign () == 14 && values.getBasicChannel () == 15 &&
               values.getRcvProgramChange () == 2 && values.getXmtProgramChange () == 0 && values.getColACC () == 118 &&
               values.getColBCC () == 100 && values.getColCCC () == -1 && values.getPitchWheelSemi () == 48 &&
               values.getVelocityDepth () == 127 && values.getNotifications () == 0 && values.getIndexBaseKey () == 119,
               "Every current MIDI parameter parses without loss");
        check (document.write (file, baseline).wasOk () && file.loadFileAsString () == text, "Unedited save preserves exact text and line endings");
        values.setPitchWheelSemi (24, false);
        check (document.write (file, baseline).wasOk (), "Edit known field while keeping future fields");
        const auto expected { text.replace ("pitchwheelsemi : 48", "pitchwheelsemi : 24") };
        check (file.loadFileAsString () == expected, "Only changed value bytes are replaced, including inline comments and no final newline");
        MidiSetupFile reread;
        check (reread.read (file).wasOk () && MidiSetupFile::settingsEqual (baseline, reread.getMidiSetupPropertiesVT ()), "All known settings round-trip");

        values.setVelocityDepth (42, false);
        MidiSetupFile freshWriter;
        check (freshWriter.write (file, baseline).wasOk () && file.loadFileAsString () == expected.replace ("velocitydepth : 127", "velocitydepth : 42"),
               "A fresh writer also preserves unknown destination fields");

        const auto oldFile { folder.getChildFile ("legacy.yml") };
        check (oldFile.replaceWithText ("mode : 1\nassign : 0\n", false, false, nullptr), "Write legacy partial MIDI fixture");
        MidiSetupFile legacy;
        check (legacy.read (oldFile).wasOk (), "Older files may omit newer fields");
        auto edit { legacy.getMidiSetupPropertiesVT ().createCopy () };
        edit.setProperty (MidiSetupProperties::IndexBaseKeyPropertyId, 60, nullptr);
        check (legacy.write (oldFile, edit).wasOk () && oldFile.loadFileAsString () == "mode : 1\nassign : 0\nindexbasekey : 60\n",
               "Only an edited missing field is appended, not all default fields");
        MidiSetupFile reused;
        check (reused.parse (juce::StringArray::fromLines ("pitchwheelsemi : 24")).isValid (), "Parse first setup");
        check (reused.parse (juce::StringArray::fromLines ("mode : 1")).isValid (), "Reuse parser for another setup");
        check (static_cast<int> (reused.getMidiSetupPropertiesVT ().getProperty (MidiSetupProperties::PitchWheelSemiPropertyId)) == 12,
               "Parser reuse resets omitted parameters to defaults");

        MidiSetupProperties defaults;
        for (const auto* invalid : { "", "mode without colon", "mode : 1\nmode : 2", "pitchwheelsemi : 9999999999999999999999", "mode : invalid",
                                     "mode : 1junk", "mode : 1 2", "mode : 3", "assign : 8", "mode : 0\nassign : 10", "mode : 2\nassign : 1",
                                     "future:\n  nested : 1", "basicchannel : -1" })
        {
            const auto bad { folder.getChildFile ("bad.yml") };
            check (bad.replaceWithText (invalid, false, false, nullptr), "Write unsupported MIDI fixture");
            const auto before { bad.loadFileAsString () };
            MidiSetupFile invalidDocument;
            check (invalidDocument.read (bad).failed (), "Malformed or unsupported MIDI must not silently become defaults");
            check (invalidDocument.write (bad, defaults.getValueTree ()).failed () && bad.loadFileAsString () == before,
                   "Refuse to overwrite an unsupported MIDI document");
            MidiSetupFile direct;
            check (direct.write (bad, defaults.getValueTree ()).failed () && bad.loadFileAsString () == before,
                   "Fresh writer cannot bypass malformed destination protection");
        }

        const auto conflict { folder.getChildFile ("conflict.yml") };
        MidiSetupFile writer;
        check (writer.write (conflict, defaults.getValueTree ()).wasOk (), "Create MIDI conflict fixture");
        check (conflict.appendText ("# external edit\n", false, false, nullptr), "Simulate a concurrent disk edit");
        const auto external { conflict.loadFileAsString () };
        check (writer.write (conflict, defaults.getValueTree ()).failed () && conflict.loadFileAsString () == external,
               "Loaded writer refuses external modifications");
        const auto appeared { folder.getChildFile ("appeared.yml") };
        MidiSetupFile missing;
        check (missing.read (appeared).wasOk () && appeared.replaceWithText ("mode : 0\n", false, false, nullptr), "Load a missing slot, then create it externally");
        check (missing.write (appeared, defaults.getValueTree ()).failed () && appeared.loadFileAsString () == "mode : 0\n",
               "Previously missing slot cannot overwrite a newly appeared file");
        const auto blocked { folder.getChildFile ("blocked.yml") };
        check (blocked.createDirectory ().wasOk () && writer.write (blocked, defaults.getValueTree ()).failed (), "Directory destinations fail safely");

        const auto bomFile { folder.getChildFile ("windows.yml") };
        const auto bom { juce::String::charToString (static_cast<juce::juce_wchar> (0xfeff)) };
        const auto windowsText { bom + "mode : 1\r\npitchwheelsemi : 12\r\nfuture: keep\r\n" };
        check (bomFile.replaceWithText (windowsText, false, false, nullptr), "Create a Windows UTF-8 BOM fixture");
        MidiSetupFile windows;
        check (windows.read (bomFile).wasOk (), "Accept UTF-8 BOMs without treating them as a field name");
        auto windowsEdit { windows.getMidiSetupPropertiesVT ().createCopy () };
        windowsEdit.setProperty (MidiSetupProperties::ModePropertyId, 0, nullptr);
        check (windows.write (bomFile, windowsEdit).wasOk (), "Edit the first field after a UTF-8 BOM");
        juce::MemoryBlock windowsBytes;
        const auto windowsExpected { windowsText.replace ("mode : 1", "mode : 0") };
        check (bomFile.loadFileAsData (windowsBytes) && windowsBytes == juce::MemoryBlock (windowsExpected.toRawUTF8 (), windowsExpected.getNumBytesAsUTF8 ()),
               "Preserve Windows BOM, CRLF endings and future fields byte-for-byte");
        auto directEdit { windows.getMidiSetupPropertiesVT () };
        directEdit.setProperty (MidiSetupProperties::PitchWheelSemiPropertyId, 48, nullptr);
        check (windows.write (bomFile, directEdit).wasOk () && bomFile.loadFileAsString ().contains ("pitchwheelsemi : 48"),
               "Editing a returned tree cannot mutate the document baseline or discard the edit");
        MidiSetupFile parsed;
        auto parsedEdit { parsed.parse (juce::StringArray::fromLines ("mode : 1\npitchwheelsemi : 12\nfuture: preserve")) };
        parsedEdit.setProperty (MidiSetupProperties::PitchWheelSemiPropertyId, 36, nullptr);
        const auto parsedFile { folder.getChildFile ("parsed.yml") };
        check (parsed.write (parsedFile, parsedEdit).wasOk () && parsedFile.loadFileAsString ().contains ("pitchwheelsemi : 36") &&
               parsedFile.loadFileAsString ().contains ("future: preserve"), "Editing parse() output also keeps an independent immutable baseline");
    }
}

struct MidiSavingTestAccess
{
    static void run (const juce::File& folder)
    {
        check (folder.createDirectory ().wasOk (), "Create MIDI dialog fixture folder");
        const auto first { folder.getChildFile ("midi1.yml") }, second { folder.getChildFile ("midi2.yml") }, invalid { folder.getChildFile ("midi3.yml") };
        check (first.replaceWithText ("mode : 1\npitchwheelsemi : 12\nfuture: keep\n", false, false, nullptr) &&
               second.replaceWithText ("# unchanged\nmode : 1\n", false, false, nullptr) && invalid.replaceWithText ("mode : invalid\n", false, false, nullptr), "Create mixed readable/unreadable MIDI slots");
        const auto previousTime { juce::Time::getCurrentTime () - juce::RelativeTime::days (2) };
        check (second.setLastModificationTime (previousTime), "Set unrelated file timestamp");
        const auto unchangedTime { second.getLastModificationTime () };
        juce::ValueTree root { "Root" };
        PersistentRootProperties persistent (root, PersistentRootProperties::WrapperType::owner, PersistentRootProperties::EnableCallbacks::no);
        RuntimeRootProperties runtime (root, RuntimeRootProperties::WrapperType::owner, RuntimeRootProperties::EnableCallbacks::no);
        AppProperties app;
        app.wrap (persistent.getValueTree (), AppProperties::WrapperType::owner, AppProperties::EnableCallbacks::no);
        GuiControlProperties gui (runtime.getValueTree (), GuiControlProperties::WrapperType::owner, GuiControlProperties::EnableCallbacks::no);
        app.setMostRecentFolder (folder.getFullPathName ());
        MidiConfigDialogComponent dialog;
        dialog.init (root);
        check (dialog.loadMidiSetups ().failed (), "Dialog reports an unreadable slot");
        check (! dialog.midiSetupEditorComponents[2].isEnabled () && dialog.midiSetupEditorComponents[0].isEnabled (),
               "Only the unreadable MIDI slot is disabled");
        dialog.midiSetupPropertiesListVT.getChild (0).setProperty (MidiSetupProperties::PitchWheelSemiPropertyId, 36, nullptr);
        const auto otherFolder { folder.getChildFile ("different-folder") };
        check (otherFolder.createDirectory ().wasOk (), "Create alternate folder");
        app.setMostRecentFolder (otherFolder.getFullPathName ());
        check (dialog.saveMidiSetups ().wasOk (), "Save dirty valid slot despite an unrelated malformed slot");
        check (first.loadFileAsString () == "mode : 1\npitchwheelsemi : 36\nfuture: keep\n", "Dialog preserves future keys and binds saves to loaded folder");
        check (second.loadFileAsString () == "# unchanged\nmode : 1\n" && second.getLastModificationTime () == unchangedTime,
               "Unrelated setup is never rewritten");
        check (invalid.loadFileAsString () == "mode : invalid\n" && ! folder.getChildFile ("midi4.yml").exists () &&
               ! otherFolder.getChildFile ("midi1.yml").exists (), "Unreadable and absent slots remain untouched");
        check (! dialog.isSetupEdited (0) && ! dialog.anyMidiSetupsEdited, "Successful save updates dirty baseline");

        app.setMostRecentFolder (folder.getFullPathName ());
        check (dialog.loadMidiSetups ().failed (), "Reload known unreadable fixture");
        dialog.midiSetupPropertiesListVT.getChild (0).setProperty (MidiSetupProperties::PitchWheelSemiPropertyId, 24, nullptr);
        dialog.midiSetupPropertiesListVT.getChild (1).setProperty (MidiSetupProperties::PitchWheelSemiPropertyId, 24, nullptr);
        check (second.appendText ("# changed externally\n", false, false, nullptr), "Change later dirty slot externally");
        const auto firstBefore { first.loadFileAsString () }, secondBefore { second.loadFileAsString () };
        check (dialog.saveMidiSetups ().failed () && first.loadFileAsString () == firstBefore && second.loadFileAsString () == secondBefore,
               "Batch preflight refuses a later conflict before saving earlier edits");
        check (dialog.isSetupEdited (0) && dialog.isSetupEdited (1), "Failed preflight keeps unsaved edits available");
    }
};

void testMidiSaving ()
{
    const auto folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-midi-saving", "", false) };
    check (folder.createDirectory ().wasOk (), "Create MIDI regression sandbox");
    struct Cleanup { juce::File folder; ~Cleanup () { folder.deleteRecursively (); } } cleanup { folder };
    testDocuments (folder);
    MidiSavingTestAccess::run (folder.getChildFile ("dialog"));
}
