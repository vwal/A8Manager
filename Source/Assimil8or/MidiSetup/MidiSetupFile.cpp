#include "MidiSetupFile.h"

namespace
{
    struct Parameter
    {
        juce::String name;
        juce::Identifier property;
        int minimum, maximum;
    };

    const std::array<Parameter, 12> parameters {{
        { MidiSetup::ModeId, MidiSetupProperties::ModePropertyId, 0, 2 },
        { MidiSetup::AssignId, MidiSetupProperties::AssignPropertyId, 0, 14 },
        { MidiSetup::BasicChannelId, MidiSetupProperties::BasicChannelPropertyId, 0, 15 },
        { MidiSetup::RcvProgramChangeId, MidiSetupProperties::RcvProgramChangePropertyId, 0, 2 },
        { MidiSetup::XmtProgramChangeId, MidiSetupProperties::XmtProgramChangePropertyId, 0, 1 },
        { MidiSetup::ColACCId, MidiSetupProperties::ColACCPropertyId, -1, 118 },
        { MidiSetup::ColBCCId, MidiSetupProperties::ColBCCPropertyId, -1, 118 },
        { MidiSetup::ColCCCId, MidiSetupProperties::ColCCCPropertyId, -1, 118 },
        { MidiSetup::PitchWheelSemiId, MidiSetupProperties::PitchWheelSemiPropertyId, 0, 48 },
        { MidiSetup::VelocityDepthId, MidiSetupProperties::VelocityDepthPropertyId, 0, 127 },
        { MidiSetup::NotificationsId, MidiSetupProperties::NotificationsPropertyId, 0, 1 },
        { MidiSetup::IndexBaseKeyId, MidiSetupProperties::IndexBaseKeyPropertyId, 0, 120 }
    }};

    bool validValue (const Parameter& parameter, juce::int64 value)
    {
        return value >= parameter.minimum && value <= parameter.maximum &&
               (parameter.name != MidiSetup::AssignId || value <= 6 || value >= 10);
    }

    bool validAssignment (juce::ValueTree tree)
    {
        const auto mode { static_cast<int> (tree.getProperty (MidiSetupProperties::ModePropertyId)) };
        const auto assign { static_cast<int> (tree.getProperty (MidiSetupProperties::AssignPropertyId)) };
        return mode == 2 ? assign >= 10 && assign <= 14 : assign >= 0 && assign <= 6;
    }

    juce::Result readBytes (const juce::File& file, juce::MemoryBlock& bytes)
    {
        bytes.reset ();
        juce::FileInputStream stream (file);
        if (stream.getStatus ().failed ())
            return juce::Result::fail ("Unable to read '" + file.getFullPathName () + "': " + stream.getStatus ().getErrorMessage ());
        const auto length { stream.getTotalLength () };
        // MIDI setups are small text files. Refuse implausibly large or binary inputs.
        if (length < 0 || length > 1024 * 1024 ||
            stream.readIntoMemoryBlock (bytes) != static_cast<size_t> (length) || stream.getStatus ().failed ())
            return juce::Result::fail ("Unable to read the complete MIDI setup '" + file.getFullPathName () + "'.");
        return juce::Result::ok ();
    }
}

juce::Result MidiSetupFile::read (juce::File file)
{
    sourceFile = file;
    sourceExisted = file.exists ();
    sourceBytes.reset ();
    if (! sourceExisted)
        return parseText ({});

    parseResult = readBytes (file, sourceBytes);
    if (parseResult.failed ())
        return parseResult;
    const auto* data { static_cast<const char*> (sourceBytes.getData ()) };
    if (sourceBytes.getSize () == 0 || std::memchr (data, 0, sourceBytes.getSize ()) != nullptr ||
        ! juce::CharPointer_UTF8::isValidString (data, static_cast<int> (sourceBytes.getSize ())))
        return parseResult = juce::Result::fail ("The MIDI setup is empty or is not supported UTF-8 text. The file has not been changed.");
    return parseText (juce::String::fromUTF8 (data, static_cast<int> (sourceBytes.getSize ())));
}

juce::ValueTree MidiSetupFile::parse (juce::StringArray lines)
{
    sourceFile = juce::File {};
    sourceBytes.reset ();
    sourceExisted = false;
    parseText (lines.joinIntoString ("\n"));
    return parseResult.wasOk () ? getMidiSetupPropertiesVT () : juce::ValueTree {};
}

juce::Result MidiSetupFile::parseText (const juce::String& text)
{
    sourceText = text;
    sourceLines.clear ();
    midiSetupProperties.wrap ({}, MidiSetupProperties::WrapperType::owner, MidiSetupProperties::EnableCallbacks::no);
    juce::StringArray keys;
    auto fail = [this] (const juce::String& reason)
    {
        return parseResult = juce::Result::fail ("MIDI setup line " + juce::String (sourceLines.size ()) + ": " + reason +
                                               " The original file will not be overwritten.");
    };

    for (int start { 0 }; start < text.length ();)
    {
        const auto newline { text.indexOf (start, "\n") };
        const auto end { newline < 0 ? text.length () : newline };
        auto content { text.substring (start, end) };
        juce::String ending { newline < 0 ? "" : "\n" };
        if (content.endsWithChar ('\r'))
        {
            content = content.dropLastCharacters (1);
            ending = "\r" + ending;
        }
        sourceLines.push_back ({ content, ending });
        start = end + 1;
        auto& line { sourceLines.back () };
        const auto bomLength { sourceLines.size () == 1 && content.startsWithChar (static_cast<juce::juce_wchar> (0xfeff)) ? 1 : 0 };
        if (bomLength != 0)
            content = content.substring (bomLength);
        const auto trimmed { content.trim () };
        if (trimmed.isEmpty () || trimmed.startsWithChar ('#'))
            continue;
        if (content != content.trimStart ())
            return fail ("Indented/nested MIDI fields are not supported for editing.");
        const auto colon { content.indexOfChar (':') };
        if (colon < 1)
            return fail ("Expected a field name followed by ':'.");
        const auto key { content.substring (0, colon).trim () };
        if (! key.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"))
            return fail ("Unsupported field name '" + key + "'.");
        if (keys.contains (key))
            return fail ("Duplicate field '" + key + "'.");
        keys.add (key);
        if (content.substring (colon + 1).trim ().isEmpty ())
            return fail ("Empty or nested field '" + key + "'.");

        for (size_t index { 0 }; index < parameters.size (); ++index)
        {
            const auto& parameter { parameters[index] };
            if (key != parameter.name)
                continue;
            line.parameter = static_cast<int> (index);
            line.valueStart = colon + 1;
            while (line.valueStart < content.length () && juce::CharacterFunctions::isWhitespace (content[line.valueStart]))
                ++line.valueStart;
            line.valueEnd = line.valueStart;
            while (line.valueEnd < content.length () && ! juce::CharacterFunctions::isWhitespace (content[line.valueEnd]) && content[line.valueEnd] != '#')
                ++line.valueEnd;
            const auto token { content.substring (line.valueStart, line.valueEnd) };
            const auto digits { token.startsWithChar ('-') || token.startsWithChar ('+') ? token.substring (1) : token };
            const auto suffix { content.substring (line.valueEnd).trimStart () };
            if (digits.isEmpty () || digits.length () > 10 || ! digits.containsOnly ("0123456789") ||
                (suffix.isNotEmpty () && ! suffix.startsWithChar ('#')) || ! validValue (parameter, token.getLargeIntValue ()))
                return fail ("Invalid or unsupported value for '" + key + "'.");
            midiSetupProperties.getValueTree ().setProperty (parameter.property, token.getIntValue (), nullptr);
            line.valueStart += bomLength;
            line.valueEnd += bomLength;
            break;
        }
        // Unknown flat fields are retained verbatim, never interpreted as editor controls.
    }
    if (! validAssignment (midiSetupProperties.getValueTree ()))
        return fail ("The mode and assignment values are incompatible or unsupported.");
    return parseResult = juce::Result::ok ();
}

bool MidiSetupFile::settingsEqual (juce::ValueTree first, juce::ValueTree second)
{
    if (! first.hasType (MidiSetupProperties::MidiSetupTypeId) || ! second.hasType (MidiSetupProperties::MidiSetupTypeId))
        return false;
    for (const auto& parameter : parameters)
        if (first.getProperty (parameter.property) != second.getProperty (parameter.property))
            return false;
    return true;
}

juce::Result MidiSetupFile::checkForExternalChanges () const
{
    if (parseResult.failed ())
        return parseResult;
    if (sourceFile == juce::File {})
        return juce::Result::ok ();
    if (sourceFile.exists () != sourceExisted)
        return juce::Result::fail ("'" + sourceFile.getFileName () + "' was created or removed outside this editor. Reopen MIDI settings before saving.");
    if (sourceExisted)
    {
        juce::MemoryBlock current;
        const auto result { readBytes (sourceFile, current) };
        if (result.failed ())
            return result;
        if (current != sourceBytes)
            return juce::Result::fail ("'" + sourceFile.getFileName () + "' changed outside this editor. Reopen MIDI settings before saving.");
    }
    return juce::Result::ok ();
}

juce::String MidiSetupFile::render (juce::ValueTree properties)
{
    juce::String output;
    std::array<bool, 12> present {};
    for (const auto& line : sourceLines)
    {
        if (line.parameter < 0)
            output += line.text;
        else
        {
            const auto index { static_cast<size_t> (line.parameter) };
            present[index] = true;
            const auto value { properties.getProperty (parameters[index].property) };
            if (value == midiSetupProperties.getValueTree ().getProperty (parameters[index].property))
                output += line.text;
            else
                output += line.text.substring (0, line.valueStart) + value.toString () + line.text.substring (line.valueEnd);
        }
        output += line.ending;
    }
    const juce::String ending { sourceText.contains ("\r\n") ? "\r\n" : "\n" };
    if (sourceText.isEmpty ())
        output = "# MIDI Setup" + ending;
    for (size_t index { 0 }; index < parameters.size (); ++index)
    {
        const auto& parameter { parameters[index] };
        const auto value { properties.getProperty (parameter.property) };
        if (! present[index] && (sourceText.isEmpty () || value != midiSetupProperties.getValueTree ().getProperty (parameter.property)))
        {
            if (output.isNotEmpty () && ! output.endsWithChar ('\n'))
                output += ending;
            output += parameter.name + " : " + value.toString () + ending;
        }
    }
    return output;
}

juce::Result MidiSetupFile::write (juce::File file, juce::ValueTree properties)
{
    if (! properties.hasType (MidiSetupProperties::MidiSetupTypeId) || file.isDirectory ())
        return juce::Result::fail ("Invalid MIDI setup or destination: " + file.getFullPathName ());
    for (const auto& parameter : parameters)
    {
        const auto value { properties.getProperty (parameter.property) };
        if ((! value.isInt () && ! value.isInt64 ()) || ! validValue (parameter, static_cast<juce::int64> (value)))
            return juce::Result::fail ("Invalid or unsupported MIDI value for '" + parameter.name + "'. The destination has not been changed.");
    }
    if (! validAssignment (properties))
        return juce::Result::fail ("The MIDI mode and assignment are incompatible. The destination has not been changed.");
    if (parseResult.failed ())
        return parseResult;
    // A fresh writer must merge the destination, not erase fields from a newer firmware.
    if (sourceFile != file && file.exists ())
    {
        MidiSetupFile existing;
        const auto result { existing.read (file) };
        return result.wasOk () ? existing.write (file, properties) : result;
    }
    if (sourceFile != file)
    {
        sourceFile = file;
        sourceExisted = false;
        sourceBytes.reset ();
    }
    auto result { checkForExternalChanges () };
    if (result.failed ())
        return result;
    const auto output { render (properties) };
    if (sourceFile == file && sourceExisted && output == sourceText)
        return juce::Result::ok ();

    juce::TemporaryFile temporary (file);
    {
        juce::FileOutputStream stream (temporary.getFile ());
        if (stream.getStatus ().failed () || ! stream.write (output.toRawUTF8 (), output.getNumBytesAsUTF8 ()))
            return juce::Result::fail ("Unable to write MIDI setup '" + file.getFullPathName () + "'. The original file has not been changed.");
        stream.flush ();
        if (stream.getStatus ().failed () || stream.getPosition () != static_cast<juce::int64> (output.getNumBytesAsUTF8 ()))
            return juce::Result::fail ("Unable to finish writing MIDI setup '" + file.getFullPathName () + "'. The original file has not been changed.");
    }
    result = checkForExternalChanges ();
    if (result.failed ())
        return result;
    if (! temporary.overwriteTargetFileWithTemporary ())
        return juce::Result::fail ("Unable to replace MIDI setup '" + file.getFullPathName () + "'. Check the destination and available space.");
    sourceFile = file;
    sourceExisted = true;
    sourceBytes.replaceAll (output.toRawUTF8 (), output.getNumBytesAsUTF8 ());
    return parseText (output);
}
