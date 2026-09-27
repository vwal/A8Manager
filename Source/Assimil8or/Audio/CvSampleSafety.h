#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <cmath>
#include <cstring>

// File-purpose provenance, not a signal classifier. Call only off the audio
// callback: legacy exports require a small, bounded adjacent-recipe read.
namespace CvSampleSafety
{
    inline constexpr auto cvMarker { "A8Manager.SamplePurpose/1=CV" };
    inline constexpr auto audioMarker { "A8Manager.SamplePurpose/1=AUDIO" };

    namespace detail
    {
        inline bool hasMarker (const juce::String& comments, const juce::String& marker)
        {
            for (const auto& line : juce::StringArray::fromLines (comments))
                if (line.trim () == marker) return true;
            return false;
        }

        inline bool number (const juce::DynamicObject& object, const juce::Identifier& name, double& value)
        {
            const auto& property { object.getProperty (name) };
            if (! (property.isDouble () || property.isInt () || property.isInt64 ())) return false;
            value = static_cast<double> (property);
            return std::isfinite (value);
        }

        inline bool boundedJsonObject (const char* text, int bytes)
        {
            // JUCE's JSON parser is recursive and accepts a valid prefix with
            // trailing text. Bound nesting before parsing and require exactly
            // one complete root object; the parser still validates its syntax.
            if (! juce::CharPointer_UTF8::isValidString (text, bytes)) return false;
            std::array<char, 32> nesting {};
            size_t depth { 0 };
            bool started { false }, complete { false }, quoted { false }, escaped { false };
            auto whitespace = [] (char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
            for (int index { 0 }; index < bytes; ++index)
            {
                const auto c { text[index] };
                if (! started)
                {
                    if (whitespace (c)) continue;
                    if (c != '{') return false;
                    started = true;
                }
                if (complete)
                {
                    if (! whitespace (c)) return false;
                    continue;
                }
                if (quoted)
                {
                    if (static_cast<unsigned char> (c) < 0x20) return false;
                    if (escaped) escaped = false;
                    else if (c == '\\') escaped = true;
                    else if (c == '"') quoted = false;
                    continue;
                }
                if (c == '"') quoted = true;
                else if (c == '{' || c == '[')
                {
                    if (depth == nesting.size ()) return false;
                    nesting[depth++] = c;
                }
                else if (c == '}' || c == ']')
                {
                    if (depth == 0 || nesting[depth - 1] != (c == '}' ? '{' : '[')) return false;
                    if (--depth == 0) complete = true;
                }
            }
            return complete && ! quoted && depth == 0;
        }
    }

    inline void markCv (juce::StringPairArray& metadata)
    {
        // ICMT is the conventional RIFF INFO comment. Preserve existing text
        // and unrelated metadata; a CV declaration takes safety precedence.
        const auto key { juce::WavAudioFormat::riffInfoComment2 };
        const auto comments { metadata.getValue (key, {}) };
        if (! detail::hasMarker (comments, cvMarker))
            metadata.set (key, comments + (comments.isEmpty () || comments.endsWithChar ('\n') ? "" : "\n") + cvMarker);
    }

    inline juce::StringPairArray exportMetadata (bool cv)
    {
        juce::StringPairArray metadata;
        metadata.set (juce::WavAudioFormat::riffInfoComment2, cv ? cvMarker : audioMarker);
        return metadata;
    }

    inline bool hasCvMetadata (const juce::StringPairArray& metadata)
    {
        return detail::hasMarker (metadata.getValue (juce::WavAudioFormat::riffInfoComment2, {}), cvMarker);
    }

    inline bool isCv (const juce::File& file, const juce::AudioFormatReader& reader)
    {
        const auto comments { reader.metadataValues.getValue (juce::WavAudioFormat::riffInfoComment2, {}) };
        if (hasCvMetadata (reader.metadataValues)) return true;
        if (detail::hasMarker (comments, audioMarker)) return false;

        // Old bundles had no embedded purpose tag. Their single modulation
        // voice had this exact name, mono PCM24 format, and recipe dimensions.
        // A filename alone, amplitude or DC content never establishes purpose.
        if (! file.getFileName ().equalsIgnoreCase ("voice-01.wav") || ! file.existsAsFile ()
            || reader.numChannels != 1 || reader.bitsPerSample != 24 || reader.usesFloatingPointData
            || (reader.sampleRate != 48000.0 && reader.sampleRate != 96000.0)) return false;
        const auto recipe { file.getSiblingFile ("design.json") };
        constexpr int maximumRecipeBytes { 64 * 1024 };
        const auto bytes { recipe.getSize () };
        if (bytes <= 0 || bytes > maximumRecipeBytes) return false;
        auto stream { recipe.createInputStream () };
        if (! stream || stream->getStatus ().failed ()) return false;
        juce::MemoryBlock contents;
        stream->readIntoMemoryBlock (contents, maximumRecipeBytes + 1);
        if (contents.getSize () == 0 || contents.getSize () > maximumRecipeBytes || stream->getStatus ().failed ()
            || std::memchr (contents.getData (), 0, contents.getSize ()) != nullptr) return false;
        const auto* text { static_cast<const char*> (contents.getData ()) };
        const auto length { static_cast<int> (contents.getSize ()) };
        if (! detail::boundedJsonObject (text, length)) return false;
        const auto json { juce::JSON::parse (juce::String::fromUTF8 (text, length)) };
        const auto* root { json.getDynamicObject () };
        if (root == nullptr || ! root->getProperty ("type").isString ()
            || root->getProperty ("type").toString () != "A8Manager.WaveformDesign") return false;
        const auto& version { root->getProperty ("version") };
        if (! (version.isInt () || version.isInt64 ()) || static_cast<juce::int64> (version) != 1) return false;
        const auto* settings { root->getProperty ("settings").getDynamicObject () };
        if (settings == nullptr || ! settings->getProperty ("mode").isString ()
            || settings->getProperty ("mode").toString () != "modulation") return false;
        double rate { 0.0 }, duration { 0.0 };
        if (! detail::number (*settings, "sampleRate", rate) || ! detail::number (*settings, "durationSeconds", duration)
            || rate != reader.sampleRate || duration < 0.001 || duration > 60.0) return false;
        return reader.lengthInSamples == std::llround (rate * duration);
    }
}
