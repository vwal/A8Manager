#pragma once
#include "Assimil8orPreset.h"
#include "FileTypeHelpers.h"
#include "Preset/PresetHelpers.h"
#include "Audio/ChannelCvSafety.h"
#include <array>
#include <cstring>
#include <limits>

namespace PresetFileOperations
{
    using PresetInfo = std::tuple<int, bool, juce::String>;
    using PresetInfoList = std::array<PresetInfo, FileTypeHelpers::kMaxPresets>;

    inline int rowForSlot (const PresetInfoList& list, int count, int slot)
    {
        for (auto row { 0 }; row < juce::jlimit (0, static_cast<int> (list.size ()), count); ++row)
            if (std::get<0> (list[static_cast<size_t> (row)]) == slot) return row;
        return -1;
    }

    inline int slotAfterSwap (int selected, int from, int to)
    {
        return selected == from ? to : selected == to ? from : selected;
    }

    inline bool needsOverwriteConfirmation (const juce::File& target) { return target.exists (); }

    inline void guardedChange (juce::ValueTree edited, juce::ValueTree baseline,
                               std::function<void (std::function<void ()>, std::function<void ()>)> ask,
                               std::function<void ()> change, std::function<void ()> cancel)
    {
        if (PresetHelpers::areEntirePresetsEqual (edited, baseline)) change ();
        else ask (std::move (change), std::move (cancel));
    }

    inline juce::Result save (juce::File file, juce::ValueTree edited, juce::ValueTree baseline)
    {
        if (const auto safety { ChannelCvSafety::validatePreset (edited, file.getParentDirectory ()) }; safety.failed ()) return safety;
        Assimil8orPreset writer;
        const auto result { writer.write (file, edited) };
        if (result.wasOk ()) PresetProperties::copyTreeProperties (edited, baseline);
        return result;
    }

    inline juce::Result read (const juce::File& file, juce::ValueTree& result)
    {
        result = {};
        if (! file.existsAsFile ()) return juce::Result::fail ("Preset file is missing: " + file.getFullPathName ());
        juce::MemoryBlock bytes;
        if (! file.loadFileAsData (bytes) || bytes.getSize () > static_cast<size_t> (std::numeric_limits<int>::max ()))
            return juce::Result::fail ("Cannot read the complete preset file: " + file.getFullPathName ());
        if (bytes.getSize () != 0 && std::memchr (bytes.getData (), 0, bytes.getSize ()) != nullptr)
            return juce::Result::fail ("Cannot load '" + file.getFileName () + "': NUL bytes or unsupported text encoding. Use a UTF-8 preset file.");
        if (! juce::CharPointer_UTF8::isValidString (static_cast<const char*> (bytes.getData ()), static_cast<int> (bytes.getSize ())))
            return juce::Result::fail ("Cannot load '" + file.getFileName () + "': invalid UTF-8 encoding. The original file was not changed.");
        const auto lines { juce::StringArray::fromLines (juce::String::createStringFromData (bytes.getData (), static_cast<int> (bytes.getSize ()))) };
        auto headers { 0 };
        juce::StringArray content;
        for (const auto& line : lines)
        {
            const auto trimmed { line.trim () };
            if (trimmed.isEmpty ()) continue;
            // Preserve editor-only default-intent comments for the parser;
            // they are not preset headers or hardware parameters.
            if (trimmed.startsWithChar ('#')) { content.add (line); continue; }
            const auto key { trimmed.upToFirstOccurrenceOf (":", false, false).trim () };
            const auto type { key.upToFirstOccurrenceOf (" ", false, false) };
            if (type == "Preset" || type == "Channel" || type == "Zone")
            {
                const auto number { key.fromFirstOccurrenceOf (" ", false, false).trim () };
                const auto id { number.getIntValue () };
                if (! number.containsOnly ("0123456789") || id < 1 || id > (type == "Preset" ? 199 : 8))
                    return juce::Result::fail ("Invalid section in '" + file.getFileName () + "': " + key);
                if (type == "Preset") ++headers;
            }
            content.add (line);
        }
        if (headers != 1) return juce::Result::fail ("Expected exactly one Preset section in '" + file.getFileName () + "'.");
        Assimil8orPreset parser;
        parser.parse (content);
        if (parser.getParseErrorsVT ().getNumChildren () != 0)
            return juce::Result::fail ("Cannot load '" + file.getFileName () + "': " + parser.getParseErrorsVT ().getChild (0).getProperty ("description").toString ());
        result = parser.getPresetVT ().createCopy ();
        return juce::Result::ok ();
    }

    // Stage both complete presets before touching either live file. Backups are retained if
    // rollback itself fails, with their location included in the error for recovery.
    inline juce::Result swap (juce::File folder, int from, int to,
                             std::function<bool (juce::File, juce::File)> install = {})
    {
        if (! folder.isDirectory () || from < 1 || from > 199 || to < 1 || to > 199 || from == to)
            return juce::Result::fail ("Invalid preset move.");
        const auto fromFile { folder.getChildFile (FileTypeHelpers::getPresetFileName (from) + ".yml") };
        const auto toFile { folder.getChildFile (FileTypeHelpers::getPresetFileName (to) + ".yml") };
        if (toFile.isDirectory ()) return juce::Result::fail ("The destination is a folder.");
        const auto destinationExists { toFile.existsAsFile () };
        juce::ValueTree fromTree, toTree;
        if (auto result { read (fromFile, fromTree) }; result.failed ()) return result;
        if (destinationExists)
            if (auto result { read (toFile, toTree) }; result.failed ()) return result;
        const auto staging { folder.getNonexistentChildFile (".a8-preset-move", "", false) };
        if (auto result { staging.createDirectory () }; result.failed ()) return result;
        const auto fromBackup { staging.getChildFile ("original-from.yml") };
        const auto toBackup { staging.getChildFile ("original-to.yml") };
        auto abandon = [&] (juce::String reason) { staging.deleteRecursively (); return juce::Result::fail (reason); };
        if (! fromFile.copyFileTo (fromBackup) || (destinationExists && ! toFile.copyFileTo (toBackup)))
            return abandon ("Unable to back up both presets; nothing was moved.");
        const auto newFrom { staging.getChildFile (FileTypeHelpers::getPresetFileName (from) + ".yml") };
        const auto newTo { staging.getChildFile (FileTypeHelpers::getPresetFileName (to) + ".yml") };
        Assimil8orPreset writer;
        if (auto result { writer.write (newTo, fromTree, folder) }; result.failed ()) return abandon (result.getErrorMessage ());
        if (destinationExists)
            if (auto result { writer.write (newFrom, toTree, folder) }; result.failed ()) return abandon (result.getErrorMessage ());
        if (! install) install = [] (juce::File source, juce::File target) { return source.replaceFileIn (target); };
        if (! install (newTo, toFile) || (destinationExists ? ! install (newFrom, fromFile) : ! fromFile.deleteFile ()))
        {
            const auto restoredFrom { fromBackup.copyFileTo (fromFile) };
            const auto restoredTo { destinationExists ? toBackup.copyFileTo (toFile) : toFile.deleteFile () };
            if (! restoredFrom || ! restoredTo)
                return juce::Result::fail ("Move failed. Recovery copies are in '" + staging.getFullPathName () + "'. Do not delete them.");
            return abandon ("Move failed; the original presets were restored.");
        }
        staging.deleteRecursively ();
        return juce::Result::ok ();
    }
}
