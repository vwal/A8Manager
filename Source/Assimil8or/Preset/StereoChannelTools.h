#pragma once

#include "ZonePurge.h"
#include <algorithm>
#include <array>

namespace StereoChannelTools
{
    // Settings-only operations retain the destination's zone ranges, so retain
    // the editor permission that makes those ranges editable as well. Patch a
    // detached source first: restoring the flag afterward would briefly notify
    // observers that still-existing external loops had become disallowed.
    inline bool copySettingsPreservingLoopPermission (juce::ValueTree channelTree, juce::ValueTree source)
    {
        if (! ChannelProperties::isChannelPropertiesVT (channelTree) || ! ChannelProperties::isChannelPropertiesVT (source)) return false;
        ChannelProperties channel (channelTree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        const auto settings { source.createCopy () };
        ChannelProperties (settings, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no)
            .setAllowLoopOutsideSample (channel.getAllowLoopOutsideSample (), false);
        channel.copyFrom (settings);
        return true;
    }

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
            // Default deliberately unlinks the pair, resetting both channels to
            // Master. copyFrom copies only channel settings, never IDs or zones.
            // Reset the right side first so callbacks never see an orphaned R.
            if (channel.getChannelMode () == ChannelProperties::ChannelMode::stereoRight)
            {
                copySettingsPreservingLoopPermission (channelTree, defaults);
                copySettingsPreservingLoopPermission (partnerTree, defaults);
            }
            else
            {
                copySettingsPreservingLoopPermission (partnerTree, defaults);
                copySettingsPreservingLoopPermission (channelTree, defaults);
            }
        }
        else
            copySettingsPreservingLoopPermission (channelTree, defaults);
        return true;
    }

    inline bool purge (juce::ValueTree channelTree, juce::ValueTree defaults)
    {
        auto validChannel = [] (juce::ValueTree tree)
        {
            if (! ChannelProperties::isChannelPropertiesVT (tree) || tree.getNumChildren () != 8) return false;
            ChannelProperties channel (tree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            if (channel.getId () < 1 || channel.getId () > 8) return false;
            for (int index { 0 }; index < 8; ++index)
            {
                const auto zone { tree.getChild (index) };
                if (! zone.hasType (ZoneProperties::ZoneTypeId)
                    || static_cast<int> (zone.getProperty (ZoneProperties::IdPropertyId)) != index + 1) return false;
            }
            return true;
        };
        if (! validChannel (channelTree) || ! validChannel (defaults)) return false;
        ChannelProperties defaultChannel (defaults, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        if (defaultChannel.getChannelMode () != ChannelProperties::ChannelMode::master) return false;
        for (int index { 0 }; index < 8; ++index)
            if (ZoneProperties (defaults.getChild (index), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no).getSample ().isNotEmpty ())
                return false;

        const auto partnerTree { partner (channelTree) };
        if (partnerTree.isValid () && ! validChannel (partnerTree)) return false;
        ChannelProperties channel (channelTree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        if (channel.getChannelMode () == ChannelProperties::ChannelMode::stereoRight && ! partnerTree.isValid ()) return false;
        std::array<juce::ValueTree, 2> targets { channelTree, partnerTree };
        const auto count { partnerTree.isValid () ? 2 : 1 };
        for (int target { 0 }; target < count; ++target)
        {
            const auto next { targets[target].getParent ().getChildWithProperty (ChannelProperties::IdPropertyId,
                             static_cast<int> (targets[target].getProperty (ChannelProperties::IdPropertyId)) + 1) };
            // Do not turn an orphaned R outside this operation into a new
            // partner when the preceding target is reset to Master.
            if (ChannelProperties::isChannelPropertiesVT (next) && next != channelTree && next != partnerTree
                && ChannelProperties (next, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no).getChannelMode () == ChannelProperties::stereoRight)
                return false;
        }
        const auto savedDefaults { defaults.createCopy () };
        if (count == 2 && channel.getChannelMode () != ChannelProperties::ChannelMode::stereoRight)
            std::swap (targets[0], targets[1]);

        // Unpair the right side first, before any zone notifications. This
        // prevents synchronized callbacks from re-populating either side.
        for (int target { 0 }; target < count; ++target)
            ChannelProperties (targets[target], ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no)
                .setChannelMode (ChannelProperties::ChannelMode::master, false);
        for (int target { 0 }; target < count; ++target)
            for (int index { 0 }; index < 8; ++index)
                ZoneProperties (targets[target].getChild (index), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no)
                    .copyFrom (savedDefaults.getChild (index), false);

        // Restore Mix and other channel settings only after every sample
        // reference has gone, so active CV safety callbacks no longer mute an
        // empty channel. copyFrom retains channel/zone IDs and tree identities.
        // Purge changes preset references only; it never deletes audio files.
        for (int target { 0 }; target < count; ++target)
            ChannelProperties (targets[target], ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no).copyFrom (savedDefaults);
        return true;
    }
}
