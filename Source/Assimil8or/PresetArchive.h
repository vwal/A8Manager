#pragma once
#include "PresetFileOperations.h"
#include "Audio/SafeAudioImport.h"
#include <set>

namespace PresetArchive
{
    inline bool plainName (juce::String name)
    {
        return name.isNotEmpty () && name != "." && name != ".." && ! name.containsAnyOf ("/\\:") && ! name.startsWithChar ('.');
    }

    inline bool sameContents (juce::File first, juce::File second)
    {
        if (! first.existsAsFile () || ! second.existsAsFile () || first.getSize () != second.getSize ()) return false;
        auto a { first.createInputStream () }, b { second.createInputStream () };
        if (a == nullptr || b == nullptr) return false;
        std::array<char, 65536> left {}, right {};
        while (! a->isExhausted ())
        {
            const auto count { a->read (left.data (), static_cast<int> (left.size ())) };
            if (count <= 0 || b->read (right.data (), count) != count || ! std::equal (left.begin (), left.begin () + count, right.begin ())) return false;
        }
        return b->isExhausted ();
    }

    inline juce::Result copyNew (juce::File source, juce::File target)
    {
        return SafeAudioImport::copyNew (source, target);
    }

    // Extraction is isolated from the live root. Name/path checks and all collisions are
    // resolved before copying anything. Existing samples are never replaced; identical
    // files can be reused when importing another preset from the same sample collection.
    inline juce::Result importPreset (juce::File archiveFile, juce::File destination, juce::ValueTree& imported)
    {
        imported = {};
        if (! archiveFile.existsAsFile () || ! destination.isDirectory ()) return juce::Result::fail ("The archive or destination folder is missing.");
        const auto staging { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a8-preset-import", "", false) };
        if (auto result { staging.createDirectory () }; result.failed ()) return result;
        struct Cleanup { juce::File directory; ~Cleanup () { directory.deleteRecursively (); } } cleanup { staging };
        juce::ZipFile archive (archiveFile);
        juce::File presetFile;
        std::set<juce::String> names;
        if (archive.getNumEntries () < 1 || archive.getNumEntries () > 65)
            return juce::Result::fail ("Expected one preset and at most 64 samples in the archive.");
        for (auto i { 0 }; i < archive.getNumEntries (); ++i)
        {
            const auto* entry { archive.getEntry (i) };
            if (entry == nullptr || entry->isSymbolicLink || ! plainName (entry->filename) || ! names.insert (entry->filename.toLowerCase ()).second)
                return juce::Result::fail ("The archive contains duplicate names, folders, links, or unsafe paths. Use a flat preset export.");
            const auto file { staging.getChildFile (entry->filename) };
            if (file.hasFileExtension ("yml"))
            {
                if (presetFile != juce::File ()) return juce::Result::fail ("The archive contains more than one preset.");
                presetFile = file;
            }
            else if (! file.hasFileExtension ("wav") || entry->filename.length () > 47)
                return juce::Result::fail ("Only one preset and WAV samples with names of at most 47 characters are supported.");
            const auto result { archive.uncompressEntry (i, staging, juce::ZipFile::OverwriteFiles::no, juce::ZipFile::FollowSymlinks::no) };
            if (result.failed () || ! file.existsAsFile () || file.getSize () != entry->uncompressedSize)
                return juce::Result::fail ("Unable to extract '" + entry->filename + "'. No destination files were changed.");
        }
        juce::ValueTree tree;
        if (auto result { PresetFileOperations::read (presetFile, tree) }; result.failed ()) return result;
        std::set<juce::String> references;
        PresetProperties preset (tree, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        preset.forEachChannel ([&] (juce::ValueTree channelTree, int)
        {
            ChannelProperties channel (channelTree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            channel.forEachZone ([&] (juce::ValueTree zoneTree, int)
            {
                ZoneProperties zone (zoneTree, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                if (zone.getSample ().isNotEmpty ()) references.insert (zone.getSample ());
                return true;
            });
            return true;
        });
        juce::AudioFormatManager formats;
        formats.registerBasicFormats ();
        std::vector<std::pair<juce::File, juce::File>> copies;
        for (const auto& name : references)
        {
            if (! plainName (name) || name.length () > 47 || ! name.endsWithIgnoreCase (".wav"))
                return juce::Result::fail ("Invalid sample reference: " + name);
            const auto source { staging.getChildFile (name) }, target { destination.getChildFile (name) };
            if (target.isSymbolicLink () || target.isDirectory ()) return juce::Result::fail ("Sample destination is not a regular file: " + name);
            if (source.existsAsFile () && target.exists () && ! sameContents (source, target))
                return juce::Result::fail ("A different sample named '" + name + "' already exists. Import into another folder or rename it first; nothing was overwritten.");
            const auto audio { source.existsAsFile () ? source : target };
            auto reader { std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (audio)) };
            if (reader == nullptr || reader->numChannels < 1 || reader->numChannels > 2 || reader->lengthInSamples < 1 ||
                reader->usesFloatingPointData || ! std::isfinite (reader->sampleRate) || reader->sampleRate <= 0 || reader->sampleRate > 192000)
                return juce::Result::fail ("Missing or unsupported sample: " + name);
            if (source.existsAsFile () && ! target.exists ()) copies.emplace_back (source, target);
        }
        std::vector<juce::File> created;
        for (const auto& [source, target] : copies)
        {
            // Recheck immediately before copying; another import may have populated it.
            if (target.exists ())
            {
                for (const auto& file : created) file.deleteFile ();
                return juce::Result::fail ("The destination changed during import. No existing files were overwritten.");
            }
            if (const auto result { copyNew (source, target) }; result.failed ())
            {
                // The shared copier removes only its own partial output on failure;
                // never delete a target which may belong to another process.
                for (const auto& file : created) file.deleteFile ();
                return juce::Result::fail (result.getErrorMessage () + " The preset was not changed.");
            }
            created.push_back (target);
        }
        imported = tree;
        return juce::Result::ok ();
    }
}
