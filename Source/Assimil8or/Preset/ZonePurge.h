#pragma once
#include "PresetProperties.h"
#include "ChannelProperties.h"
#include "ZoneProperties.h"
#include <array>
#include <vector>

namespace ZonePurge
{
    inline juce::ValueTree linkedRightChannel (juce::ValueTree channelTree)
    {
        if (! channelTree.hasType (ChannelProperties::ChannelTypeId)) return {};
        ChannelProperties channel (channelTree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        if (! channelTree.isValid () || channel.getChannelMode () == ChannelProperties::ChannelMode::stereoRight ||
            channel.getId () < 1 || channel.getId () >= 8 || ! channelTree.getParent ().hasType (PresetProperties::PresetTypeId)) return {};
        const auto next { channelTree.getParent ().getChildWithProperty (ChannelProperties::IdPropertyId, channel.getId () + 1) };
        if (! next.hasType (ChannelProperties::ChannelTypeId)) return {};
        ChannelProperties right (next, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        return right.getChannelMode () == ChannelProperties::ChannelMode::stereoRight ? next : juce::ValueTree {};
    }

    inline bool apply (juce::ValueTree channelTree, int index, juce::ValueTree defaults)
    {
        if (! ChannelProperties::isChannelPropertiesVT (channelTree) || ! defaults.hasType (ZoneProperties::ZoneTypeId) || index < 0 || index >= 8) return false;
        ChannelProperties channel (channelTree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        auto zoneCount = [] (ChannelProperties& properties)
        {
            auto count { 0 };
            properties.forEachZone ([&] (juce::ValueTree, int) { ++count; return true; });
            return count;
        };
        if (zoneCount (channel) != 8 || channel.getChannelMode () == ChannelProperties::ChannelMode::stereoRight) return false;
        const auto rightTree { linkedRightChannel (channelTree) };
        ChannelProperties right (rightTree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        if (rightTree.isValid () && zoneCount (right) != 8) return false;
        std::array<juce::ValueTree, 8> leftCopies, rightCopies;
        std::vector<int> survivors;
        for (auto i { 0 }; i < 8; ++i)
        {
            leftCopies[static_cast<size_t> (i)] = channel.getZoneVT (i).createCopy ();
            if (rightTree.isValid ()) rightCopies[static_cast<size_t> (i)] = right.getZoneVT (i).createCopy ();
            ZoneProperties zone (channel.getZoneVT (i), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            if (i != index && zone.getSample ().isNotEmpty ()) survivors.push_back (i);
        }
        for (auto i { 0 }; i < 8; ++i)
        {
            const auto occupied { static_cast<size_t> (i) < survivors.size () };
            ZoneProperties target (channel.getZoneVT (i), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            target.copyFrom (occupied ? leftCopies[static_cast<size_t> (survivors[static_cast<size_t> (i)])] : defaults, false);
            if (occupied && static_cast<size_t> (i + 1) == survivors.size ()) target.setMinVoltage (-5.0, false);
            if (rightTree.isValid ())
            {
                ZoneProperties rightTarget (right.getZoneVT (i), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                rightTarget.copyFrom (occupied ? rightCopies[static_cast<size_t> (survivors[static_cast<size_t> (i)])] : defaults, false);
                if (occupied) rightTarget.setMinVoltage (target.getMinVoltage (), false);
            }
        }
        return true;
    }
}
