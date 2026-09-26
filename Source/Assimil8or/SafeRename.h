#pragma once
#include <JuceHeader.h>

namespace SafeRename
{
    inline juce::String validLeafStem (juce::String name)
    {
        const juce::String valid { " !#$%&'()+,-.0123456789;=@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]_`{}~abcdefghijklmnopqrstuvwxyz" };
        name = name.retainCharacters (valid).trim ().trimCharactersAtStart (".");
        return name.isEmpty () ? "sample" : name;
    }

    inline juce::Result automaticDestination (const juce::File& source, juce::File& target)
    {
        if (! source.exists ()) return juce::Result::fail ("The original file or folder no longer exists.");
        const auto folder { source.isDirectory () };
        const auto extension { folder ? juce::String () : source.getFileExtension () };
        const auto maxStemLength { (folder ? 31 : 47) - extension.length () };
        if (maxStemLength < 1) return juce::Result::fail ("The extension is too long to retain safely.");
        const auto stem { validLeafStem (folder ? source.getFileName () : source.getFileNameWithoutExtension ()) };
        for (int suffix { 0 }; suffix < 100000; ++suffix)
        {
            const auto tail { suffix == 0 ? juce::String () : "-" + juce::String (suffix) };
            if (tail.length () >= maxStemLength) break;
            target = source.getSiblingFile (stem.substring (0, maxStemLength - tail.length ()) + tail + extension);
            if (target == source || ! target.exists ()) return juce::Result::ok ();
        }
        return juce::Result::fail ("Unable to find an unused compatible name.");
    }

    // Validate the new leaf name, not the old one: an invalid/overlong name
    // must remain recoverable. Limits match the application's validator.
    inline juce::Result destination (const juce::File& source, juce::String name, juce::File& target)
    {
        if (! source.exists ()) return juce::Result::fail ("The original file or folder no longer exists.");
        name = name.trim ();
        const auto folder { source.isDirectory () };
        if (name.isEmpty () || name == "." || name == "..") return juce::Result::fail ("Enter a name.");
        const juce::String valid { " !#$%&'()+,-.0123456789;=@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]_`{}~abcdefghijklmnopqrstuvwxyz" };
        if (name.containsAnyOf ("/\\:") || name.removeCharacters (valid).isNotEmpty ())
            return juce::Result::fail ("Use a plain file/folder name with Assimil8or-compatible characters, not a path.");
        target = source.getParentDirectory ().getChildFile (name);
        if (! folder)
        {
            if (target.getFileExtension ().isEmpty ()) target = target.withFileExtension (source.getFileExtension ());
            if (! target.getFileExtension ().equalsIgnoreCase (source.getFileExtension ()))
                return juce::Result::fail ("Keep the original " + source.getFileExtension () + " extension (or omit it).");
        }
        if (target.getFileName ().length () > (folder ? 31 : 47))
            return juce::Result::fail ("The new name must be at most " + juce::String (folder ? 31 : 47) + " characters, including the extension.");
        if (target != source && target.exists ()) return juce::Result::fail ("That name is already in use. Nothing will be overwritten.");
        return juce::Result::ok ();
    }

    inline juce::Result apply (const juce::File& source, const juce::String& name)
    {
        juce::File target;
        const auto validation { destination (source, name, target) };
        if (validation.failed ()) return validation;
        if (target == source) return juce::Result::ok ();
        return source.moveFileTo (target) ? juce::Result::ok () : juce::Result::fail (
            "The rename failed. Check folder permissions and whether the file is in use, then try again.");
    }
}
