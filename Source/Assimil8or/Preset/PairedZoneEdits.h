#pragma once
#include "ZonePurge.h"

// Zone-list edits move the two sides as a unit, without changing channel modes
// or audio files. Detached copies avoid callbacks changing a later source mid-edit.
namespace PairedZoneEdits
{
    inline bool hasEightZones (juce::ValueTree tree)
    {
        if (! ChannelProperties::isChannelPropertiesVT (tree)) return false;
        ChannelProperties channel (tree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        int count { 0 };
        channel.forEachZone ([&count] (juce::ValueTree, int) { ++count; return true; });
        return count == 8;
    }

    inline juce::ValueTree rightZone (juce::ValueTree channel, int index)
    {
        const auto right { ZonePurge::linkedRightChannel (channel) };
        if (! right.isValid () || index < 0 || index >= 8) return {};
        ChannelProperties properties (right, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        return hasEightZones (right) ? properties.getZoneVT (index) : juce::ValueTree {};
    }

    inline bool editable (juce::ValueTree tree)
    {
        if (! ChannelProperties::isChannelPropertiesVT (tree)) return false;
        ChannelProperties channel (tree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        const auto right { ZonePurge::linkedRightChannel (tree) };
        return hasEightZones (tree) && channel.getChannelMode () != ChannelProperties::ChannelMode::stereoRight &&
               (! right.isValid () || hasEightZones (right));
    }

    inline void syncVoltages (juce::ValueTree tree)
    {
        if (! editable (tree) || ! rightZone (tree, 0).isValid ()) return;
        ChannelProperties channel (tree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        for (auto i { 0 }; i < 8; ++i)
        {
            ZoneProperties left (channel.getZoneVT (i), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            ZoneProperties right (rightZone (tree, i), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            right.setMinVoltage (left.getMinVoltage (), false);
        }
    }

    inline bool clearAll (juce::ValueTree tree, juce::ValueTree defaults)
    {
        if (! editable (tree) || ! defaults.hasType (ZoneProperties::ZoneTypeId)) return false;
        ChannelProperties channel (tree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        for (auto i { 0 }; i < 8; ++i)
        {
            ZoneProperties left (channel.getZoneVT (i), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            left.copyFrom (defaults, false);
            if (const auto right { rightZone (tree, i) }; right.isValid ())
                ZoneProperties (right, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no).copyFrom (defaults, false);
        }
        return true;
    }

    inline juce::Result copyNext (juce::ValueTree tree, int index, juce::ValueTree next)
    {
        if (! editable (tree) || index < 0 || index >= 7 || ! next.hasType (ZoneProperties::ZoneTypeId))
            return juce::Result::fail ("Invalid zone copy.");
        const auto sourceRight { rightZone (tree, index) };
        auto rightCopy { sourceRight.createCopy () };
        if (sourceRight.isValid ())
        {
            ZoneProperties source (sourceRight, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            if (source.getSample ().isEmpty ()) return juce::Result::fail ("The paired right-channel source zone is empty. Assign its sample before copying the pair.");
            ZoneProperties prepared (rightCopy, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            const auto sample { prepared.getSample () };
            const auto side { prepared.getSide () };
            // Left controls the paired playback range; retain the right file/side.
            prepared.copyFrom (next, false);
            prepared.setSample (sample, false);
            prepared.setSide (side, false);
        }
        ChannelProperties channel (tree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        ZoneProperties source (channel.getZoneVT (index), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        ZoneProperties target (channel.getZoneVT (index + 1), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        const auto occupied { target.getSample ().isNotEmpty () };
        const auto voltage { target.getMinVoltage () };
        const auto lower { source.getMinVoltage () };
        const auto upper { index == 0 ? 5.0 : ZoneProperties (channel.getZoneVT (index - 1), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no).getMinVoltage () };
        target.copyFrom (next, false);
        if (rightCopy.isValid ())
            ZoneProperties (rightZone (tree, index + 1), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no).copyFrom (rightCopy, false);
        if (occupied) target.setMinVoltage (voltage, true);
        else
        {
            source.setMinVoltage ((upper + lower) / 2.0, true);
            target.setMinVoltage (lower, true);
        }
        syncVoltages (tree);
        return juce::Result::ok ();
    }

    inline bool insert (juce::ValueTree tree, int index)
    {
        if (! editable (tree) || index < 0 || index >= 7) return false;
        ChannelProperties channel (tree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        if (ZoneProperties (channel.getZoneVT (7), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no).getSample ().isNotEmpty ()) return false;
        if (const auto right { rightZone (tree, 7) }; right.isValid () &&
            ZoneProperties (right, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no).getSample ().isNotEmpty ()) return false;
        ZoneProperties original (channel.getZoneVT (index), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        if (original.getSample ().isEmpty ()) return false;
        const auto lower { original.getMinVoltage () };
        const auto upper { index == 0 ? 5.0 : ZoneProperties (channel.getZoneVT (index - 1), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no).getMinVoltage () };
        for (auto i { 6 }; i >= index; --i)
        {
            ZoneProperties (channel.getZoneVT (i + 1), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no).copyFrom (channel.getZoneVT (i).createCopy (), false);
            if (const auto right { rightZone (tree, i) }; right.isValid ())
                ZoneProperties (rightZone (tree, i + 1), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no).copyFrom (right.createCopy (), false);
        }
        original.setMinVoltage ((upper + lower) / 2.0, true);
        syncVoltages (tree);
        return true;
    }

    inline bool flip (juce::ValueTree tree, int index, int count)
    {
        if (! editable (tree) || index < 0 || count < 2 || index + count > 8) return false;
        ChannelProperties channel (tree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        for (auto offset { 0 }; offset < count / 2; ++offset)
        {
            const auto first { index + offset }, last { index + count - offset - 1 };
            auto swap = [] (juce::ValueTree a, juce::ValueTree b)
            {
                ZoneProperties left (a, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                ZoneProperties right (b, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                const auto leftVoltage { left.getMinVoltage () }, rightVoltage { right.getMinVoltage () };
                const auto copy { a.createCopy () };
                left.copyFrom (b.createCopy (), false);
                right.copyFrom (copy, false);
                left.setMinVoltage (leftVoltage, true);
                right.setMinVoltage (rightVoltage, true);
            };
            swap (channel.getZoneVT (first), channel.getZoneVT (last));
            if (rightZone (tree, first).isValid ()) swap (rightZone (tree, first), rightZone (tree, last));
        }
        syncVoltages (tree);
        return true;
    }

    inline void capture (juce::ValueTree clipboard, juce::ValueTree source, juce::ValueTree right, bool settingsOnly)
    {
        ZoneProperties target (clipboard, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        target.copyFrom (source, false);
        if (settingsOnly) target.setSample ("", false);
        clipboard.removeAllChildren (nullptr); // This tree is an internal clipboard, never a saved zone.
        if (right.isValid ())
        {
            juce::ValueTree pair { "PairedClipboard" };
            pair.addChild (right.createCopy (), -1, nullptr);
            clipboard.addChild (pair, -1, nullptr);
        }
    }

    inline bool pasteContent (juce::ValueTree tree, int index, juce::ValueTree clipboard)
    {
        if (! editable (tree) || index < 0 || index >= 8 || ! clipboard.hasType (ZoneProperties::ZoneTypeId)) return false;
        ChannelProperties channel (tree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        ZoneProperties source (clipboard, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        const auto settingsOnly { source.getSample ().isEmpty () };
        const auto rightSource { clipboard.getChildWithName ("PairedClipboard").getChild (0) };
        const auto rightTarget { rightZone (tree, index) };
        if (! settingsOnly && rightTarget.isValid () && rightSource.isValid () &&
            ZoneProperties (rightSource, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no).getSample ().isEmpty ()) return false;
        ZoneProperties left (channel.getZoneVT (index), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        const auto leftSide { left.getSide () };
        left.copyFrom (clipboard, settingsOnly);
        if (settingsOnly) left.setSide (leftSide, false);
        if (rightTarget.isValid ())
        {
            ZoneProperties right (rightTarget, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
            const auto side { right.getSide () };
            right.copyFrom (rightSource.isValid () ? rightSource : clipboard, settingsOnly);
            if (settingsOnly) right.setSide (side, false);
            right.setMinVoltage (left.getMinVoltage (), false);
        }
        return true;
    }

    inline bool explode (juce::ValueTree tree, int index, int count, juce::int64 sampleLength)
    {
        if (! editable (tree) || index < 0 || count < 2 || index + count > 8 || sampleLength / count < 4) return false;
        ChannelProperties channel (tree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
        const auto source { channel.getZoneVT (index).createCopy () };
        ZoneProperties original (source, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
        if (original.getSample ().isEmpty ()) return false;
        const auto rightSource { rightZone (tree, index).createCopy () };
        if (rightSource.isValid () && ZoneProperties (rightSource, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no).getSample ().isEmpty ()) return false;
        for (auto offset { 0 }; offset < count; ++offset)
        {
            // Distribute the remainder too, so the final slice reaches EOF.
            const auto start { sampleLength / count * offset + sampleLength % count * offset / count };
            const auto end { sampleLength / count * (offset + 1) + sampleLength % count * (offset + 1) / count };
            auto setSlice = [start, end] (juce::ValueTree target, juce::ValueTree from)
            {
                ZoneProperties zone (target, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                zone.copyFrom (from, false);
                zone.setSampleStart (start, true);
                zone.setSampleEnd (end, true);
                zone.setLoopStart (start, true);
                zone.setLoopLength (static_cast<double> (end - start), true);
            };
            setSlice (channel.getZoneVT (index + offset), source);
            if (rightSource.isValid ()) setSlice (rightZone (tree, index + offset), rightSource);
        }
        syncVoltages (tree);
        return true;
    }
}
