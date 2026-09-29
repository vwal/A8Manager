#pragma once

#include "AudioManager.h"
#include "CvSampleSafety.h"
#include "../Preset/PresetProperties.h"

// These are file-purpose checks, not guesses based on frequency or DC content.
// Run on the editing/file-operation thread, never in an audio callback.
namespace ChannelCvSafety
{
    struct Content
    {
        bool cv { false }, audio { false }, unreadable { false };
    };

    inline Content inspect (AudioManager& manager, juce::ValueTree tree, const juce::File& folder)
    {
        Content result;
        ChannelProperties channel (tree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        channel.forEachZone ([&] (juce::ValueTree zoneTree, int)
        {
            ZoneProperties zone (zoneTree, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            if (const auto name { zone.getSample () }; name.isNotEmpty ())
            {
                const auto file { folder.getChildFile (name) };
                if (auto reader { manager.getReaderFor (file) })
                {
                    if (CvSampleSafety::isCv (file, *reader)) result.cv = true;
                    else result.audio = true;
                }
                else result.unreadable = true;
            }
            return true;
        });
        return result;
    }

    inline juce::Result accepts (const Content& existing, bool cv, int channel)
    {
        const auto prefix { "Channel " + juce::String (channel + 1) + ": " };
        if (existing.unreadable)
            return juce::Result::fail (prefix + "a referenced sample is missing or unreadable, so its audio/CV purpose cannot be verified.");
        if ((cv && existing.audio) || (! cv && existing.cv))
            return juce::Result::fail (prefix + "CV and audio cannot share a channel. Choose an empty channel or purge all its zones before changing its purpose.");
        return juce::Result::ok ();
    }

    inline void mute (ChannelProperties& channel)
    {
        channel.setMixLevel (-90.0, false);
        channel.setMixMod ("Off", 0.0, false);
    }

    inline juce::Result validatePreset (juce::ValueTree tree, const juce::File& folder)
    {
        AudioManager manager;
        PresetProperties preset (tree, PresetProperties::WrapperType::client, PresetProperties::EnableCallbacks::no);
        auto result { juce::Result::ok () };
        preset.forEachChannel ([&] (juce::ValueTree channelTree, int index)
        {
            const auto content { inspect (manager, channelTree, folder) };
            if (! content.cv) return true;
            const auto prefix { "Channel " + juce::String (index + 1) + ": " };
            if (content.audio || content.unreadable)
                result = juce::Result::fail (prefix + "CV must be isolated from audio and unverifiable samples. Move that content to separate channels before saving.");
            else
            {
                ChannelProperties channel (channelTree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
                if (channel.getMixLevel () != -90.0 || std::get<0> (channel.getMixMod ()) != "Off")
                    result = juce::Result::fail (prefix + "CV requires Mix Off and Mix modulation Off before saving.");
            }
            return result.wasOk ();
        });
        return result;
    }
}
