#pragma once

#include "ZonePurge.h"

namespace StereoChannelTools
{
    // Resolve in either direction, but never treat channel 1 in Stereo Right
    // mode, or two consecutive Stereo Right channels, as a valid pair.
    inline juce::ValueTree partner (juce::ValueTree channelTree)
    {
        if (! ChannelProperties::isChannelPropertiesVT (channelTree)) return {};
        ChannelProperties channel (channelTree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        if (channel.getId () < 1 || channel.getId () > 8 || ! channelTree.getParent ().hasType (PresetProperties::PresetTypeId)) return {};
        if (channel.getChannelMode () != ChannelProperties::ChannelMode::stereoRight)
            return ZonePurge::linkedRightChannel (channelTree);
        if (channel.getId () == 1) return {};
        const auto left { channelTree.getParent ().getChildWithProperty (ChannelProperties::IdPropertyId, channel.getId () - 1) };
        return ZonePurge::linkedRightChannel (left) == channelTree ? left : juce::ValueTree {};
    }

    inline bool resetSettings (juce::ValueTree channelTree, juce::ValueTree defaults)
    {
        if (! ChannelProperties::isChannelPropertiesVT (channelTree) || ! ChannelProperties::isChannelPropertiesVT (defaults)) return false;
        ChannelProperties defaultChannel (defaults, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        if (defaultChannel.getChannelMode () != ChannelProperties::ChannelMode::master) return false;
        const auto partnerTree { partner (channelTree) };
        ChannelProperties channel (channelTree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        if (partnerTree.isValid ())
        {
            ChannelProperties pairedChannel (partnerTree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            // Default deliberately unlinks the pair, resetting both channels to
            // Master. copyFrom copies only channel settings, never IDs or zones.
            // Reset the right side first so callbacks never see an orphaned R.
            if (channel.getChannelMode () == ChannelProperties::ChannelMode::stereoRight)
            {
                channel.copyFrom (defaults);
                pairedChannel.copyFrom (defaults);
            }
            else
            {
                pairedChannel.copyFrom (defaults);
                channel.copyFrom (defaults);
            }
        }
        else
            channel.copyFrom (defaults);
        return true;
    }
}
