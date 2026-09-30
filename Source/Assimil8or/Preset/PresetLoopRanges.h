#pragma once

#include "PresetProperties.h"
#include "ZoneSampleRanges.h"
#include "../Audio/AudioManager.h"
#include <map>

namespace PresetLoopRanges
{
    // Only the editable copy changes. Deliberate loops outside SAMPLE are
    // retained and opt their channel (and stereo partner) into independent
    // editing. Only impossible loop coordinates are reset; no disk writes.
    inline juce::StringArray repair (juce::ValueTree preset, const juce::File& folder)
    {
        juce::StringArray repaired;
        AudioManager audio;
        std::map<juce::String, juce::int64> lengths;
        auto enableOutside = [&] (ChannelProperties& channel)
        {
            const auto index { channel.getId () - 1 };
            channel.setAllowLoopOutsideSample (true, false);
            if (index > 0 && channel.getChannelMode () == ChannelProperties::stereoRight)
            {
                ChannelProperties previous (preset.getChild (index - 1), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
                if (previous.getChannelMode () != ChannelProperties::stereoRight) previous.setAllowLoopOutsideSample (true, false);
            }
            if (channel.getChannelMode () != ChannelProperties::stereoRight && index >= 0 && index + 1 < preset.getNumChildren ())
            {
                ChannelProperties next (preset.getChild (index + 1), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
                if (next.getChannelMode () == ChannelProperties::stereoRight) next.setAllowLoopOutsideSample (true, false);
            }
        };
        for (const auto& channelTree : preset)
        {
            ChannelProperties channel (channelTree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            for (const auto& zoneTree : channelTree)
            {
                ZoneProperties zone (zoneTree, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                const auto name { zone.getSample () };
                if (name.isEmpty () || name.containsAnyOf ("/\\") || juce::File::isAbsolutePath (name)) continue;
                if (lengths.find (name) == lengths.end ())
                {
                    const auto reader { audio.getReaderFor (folder.getChildFile (name)) };
                    lengths[name] = reader ? reader->lengthInSamples : 0;
                }
                const auto frames { lengths[name] };
                const auto before { ZoneSampleRanges::read (zone) };
                if (! before.loopStart && ! before.loopLength) continue;
                const auto sampleStart { before.sampleStart.value_or (0) };
                const auto sampleEnd { before.sampleEnd.value_or (frames > 0 ? frames : -1) };
                const auto loopStart { before.loopStart.value_or (sampleStart) };
                const auto identity { "CH " + juce::String (channel.getId ()) + ", zone " + juce::String (zone.getId ()) };
                if (loopStart >= 0 && ! before.loopLength && frames <= 0 && (sampleEnd < 0 || (before.loopStart && loopStart >= sampleEnd)))
                {
                    // An explicit start after SAMPLE needs the missing WAV's
                    // EOF to resolve its default length. Do not invent a
                    // negative length or discard the imported coordinate.
                    if (loopStart < sampleStart || (sampleEnd >= 0 && loopStart >= sampleEnd))
                    {
                        const auto wasEnabled { channel.getAllowLoopOutsideSample () };
                        enableOutside (channel);
                        if (! wasEnabled) repaired.add (identity + ": outside-sample loop preserved; Allow loop outside sample enabled (WAV length unavailable)");
                    }
                    continue;
                }
                const auto defaultLoopEnd { before.loopStart && loopStart >= sampleEnd && frames > 0 ? frames : sampleEnd };
                const auto loopLength { before.loopLength.value_or (defaultLoopEnd >= 0 ? static_cast<double> (defaultLoopEnd) - loopStart : -1.0) };
                // Missing files do not establish EOF. Preserve an unknown
                // default length until its source can be restored.
                const auto loopEnd { static_cast<double> (loopStart) + loopLength };
                const auto fileValid { loopStart >= 0 && std::isfinite (loopLength) && loopLength >= 4.0
                    && (frames <= 0 || loopEnd <= frames) };
                if (fileValid)
                {
                    if (loopStart < sampleStart || (sampleEnd >= 0 && loopEnd > sampleEnd))
                    {
                        const auto wasEnabled { channel.getAllowLoopOutsideSample () };
                        enableOutside (channel);
                        if (! wasEnabled) repaired.add (identity + ": outside-sample loop preserved; Allow loop outside sample enabled");
                    }
                    continue;
                }
                zone.setLoopStart (-1, false);
                zone.setLoopLength (-1.0, false);
                repaired.add (identity + ": invalid file bounds or fewer than 4 loop samples; loop reset to SAMPLE");
            }
        }
        return repaired;
    }

    inline juce::String message (const juce::StringArray& repaired)
    {
        juce::StringArray shown;
        for (int i { 0 }; i < std::min (8, repaired.size ()); ++i) shown.add (repaired[i]);
        if (repaired.size () > shown.size ()) shown.add ("and " + juce::String (repaired.size () - shown.size ()) + " more");
        return shown.joinIntoString ("\n")
            + "\n\nOutside-sample loops are preserved with independent editing enabled for their channels. File-invalid or shorter-than-4-sample loops are reset to follow SAMPLE; sample boundaries are never changed. Missing WAV lengths cannot be verified."
              "\n\nThe original preset file has not been changed. SAVE IS PENDING: click SAVE to retain the editor setting and any corrected invalid loop bounds.";
    }
}
