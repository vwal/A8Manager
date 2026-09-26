#include "Assimil8or/Preset/PairedZoneEdits.h"
#include "Assimil8or/Preset/ZoneContinuation.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void check (bool condition, const char* message) { if (! condition) throw std::runtime_error (message); }

    juce::ValueTree makePair ()
    {
        juce::ValueTree preset { PresetProperties::PresetTypeId };
        for (auto side { 0 }; side < 2; ++side)
        {
            auto tree { ChannelProperties::create (side + 1) };
            preset.addChild (tree, -1, nullptr);
            ChannelProperties channel (tree, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no);
            channel.setChannelMode (side == 0 ? ChannelProperties::ChannelMode::master : ChannelProperties::ChannelMode::stereoRight, false);
            for (auto index { 0 }; index < 8; ++index)
            {
                const auto value { ZoneProperties::create (index + 1) };
                tree.addChild (value, -1, nullptr);
                ZoneProperties zone (value, ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
                zone.setMinVoltage (-5.0, false);
                zone.setSide (side, false);
                if (index == 0)
                {
                    zone.setSample (side == 0 ? "left.wav" : "right.wav", false);
                    zone.setSampleStart (100, false);
                    zone.setSampleEnd (200, false);
                    zone.setLoopStart (120, false);
                    zone.setLoopLength (40.5, false);
                    zone.setPitchOffset (3.0, false);
                }
            }
        }
        return preset;
    }

    juce::ValueTree zoneTree (juce::ValueTree preset, int side, int index)
    {
        return ChannelProperties (preset.getChild (side), ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::no).getZoneVT (index);
    }

    ZoneProperties zone (juce::ValueTree preset, int side, int index)
    {
        return { zoneTree (preset, side, index), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no };
    }
}

void testPairedZoneEdits ()
{
    auto preset { makePair () };
    const auto left { preset.getChild (0) };
    auto next { ZoneContinuation::makeNext (zoneTree (preset, 0, 0), 1000, true) };
    check (PairedZoneEdits::copyNext (left, 0, next).wasOk (), "Continue copies both sides");
    check (zone (preset, 0, 1).getSample () == "left.wav" && zone (preset, 1, 1).getSample () == "right.wav", "Continue retains distinct paired source files");
    check (zone (preset, 1, 1).getSide () == 1 && zone (preset, 1, 1).getSampleStart () == 200 && zone (preset, 1, 1).getSampleEnd () == 300, "Continue mirrors new slice and preserves R selector");
    check (zone (preset, 0, 0).getMinVoltage () == 0.0 && zone (preset, 1, 0).getMinVoltage () == 0.0 && zone (preset, 1, 1).getMinVoltage () == -5.0, "Continue splits and synchronizes voltage bounds");
    for (auto side { 0 }; side < 2; ++side) check (zone (preset, side, 1).getId () == 2, "Copy preserves destination slot identity");
    zone (preset, 0, 1).setMinVoltage (-2.0, false);
    check (PairedZoneEdits::copyNext (left, 0, zoneTree (preset, 0, 0).createCopy ()).wasOk () && zone (preset, 1, 1).getMinVoltage () == -2.0, "Replacing occupied target retains voltage");
    zone (preset, 1, 0).setSample ("", false);
    const auto before { preset.createCopy () };
    check (PairedZoneEdits::copyNext (left, 0, next).failed () && preset.isEquivalentTo (before), "Missing paired source fails before modifying left");
    check (! PairedZoneEdits::explode (left, 0, 3, 1001) && preset.isEquivalentTo (before), "Incomplete paired source cannot be exploded");
    auto incompleteClipboard { ZoneProperties::create (1) };
    PairedZoneEdits::capture (incompleteClipboard, zoneTree (preset, 0, 0), zoneTree (preset, 1, 0), false);
    check (! PairedZoneEdits::pasteContent (left, 4, incompleteClipboard) && preset.isEquivalentTo (before), "Incomplete copied pair cannot replace a target");
    zone (preset, 1, 0).setSample ("right.wav", false);

    zone (preset, 0, 1).setSample ("second-left.wav", false);
    zone (preset, 1, 1).setSample ("second-right.wav", false);
    check (PairedZoneEdits::insert (left, 0), "Insert duplicates and shifts pair");
    check (zone (preset, 0, 2).getSample () == "second-left.wav" && zone (preset, 1, 2).getSample () == "second-right.wav", "Insert moves both later zones without mismatching files");
    zone (preset, 1, 7).setSample ("do-not-drop.wav", false);
    const auto full { preset.createCopy () };
    check (! PairedZoneEdits::insert (left, 0) && preset.isEquivalentTo (full), "Insert refuses occupied final slot on either side");
    zone (preset, 1, 7).setSample ("", false);
    check (PairedZoneEdits::flip (left, 0, 3), "Flip paired zones");
    check (zone (preset, 0, 0).getSample () == "second-left.wav" && zone (preset, 1, 0).getSample () == "second-right.wav", "Flip preserves the pairing");
    check (zone (preset, 0, 0).getId () == 1 && zone (preset, 1, 2).getId () == 3, "Flip retains slot IDs");

    auto clipboard { ZoneProperties::create (1) };
    PairedZoneEdits::capture (clipboard, zoneTree (preset, 0, 0), zoneTree (preset, 1, 0), false);
    check (PairedZoneEdits::pasteContent (left, 4, clipboard), "Paste full pair");
    check (zone (preset, 0, 4).getSample () == "second-left.wav" && zone (preset, 1, 4).getSample () == "second-right.wav", "Clipboard carries both files");
    zone (preset, 0, 0).setPitchOffset (7.0, false);
    zone (preset, 1, 0).setPitchOffset (7.0, false);
    PairedZoneEdits::capture (clipboard, zoneTree (preset, 0, 0), zoneTree (preset, 1, 0), true);
    check (PairedZoneEdits::pasteContent (left, 1, clipboard), "Settings-only paste");
    check (zone (preset, 0, 1).getSample () == "left.wav" && zone (preset, 1, 1).getSample () == "right.wav" && zone (preset, 1, 1).getSide () == 1, "Settings paste retains each target file and side");
    check (zone (preset, 0, 1).getPitchOffset () == 7.0 && zone (preset, 1, 1).getPitchOffset () == 7.0, "Settings paste updates both sides");
    PairedZoneEdits::capture (clipboard, zoneTree (preset, 0, 1), {}, false);
    check (PairedZoneEdits::pasteContent (left, 5, clipboard) && zone (preset, 1, 5).getSample () == "left.wav" && zone (preset, 1, 5).getSide () == 0, "Unpaired clipboard intentionally duplicates chosen side in existing pair");

    check (PairedZoneEdits::explode (left, 1, 3, 1001), "Explode paired source");
    check (zone (preset, 0, 3).getSampleEnd () == 1001 && zone (preset, 1, 3).getSampleEnd () == 1001, "Explode covers final remainder on both sides");
    check (zone (preset, 1, 2).getSample () == "right.wav" && zone (preset, 1, 2).getSide () == 1, "Explode retains distinct right source");
    check (zone (preset, 0, 2).getSampleStart () == zone (preset, 1, 2).getSampleStart (), "Explode slice boundaries match");
    const auto valid { preset.createCopy () };
    check (! PairedZoneEdits::explode (left, 6, 3, 1001) && ! PairedZoneEdits::explode (left, 0, 8, 10) && preset.isEquivalentTo (valid), "Invalid explode is a no-op");
    check (! PairedZoneEdits::clearAll (preset.getChild (1), ZoneProperties::create (1)), "Cannot independently clear read-only stereo-right");
    const auto defaults { ZoneProperties::create (1) };
    check (PairedZoneEdits::clearAll (left, defaults), "Clear paired channel");
    for (auto side { 0 }; side < 2; ++side)
        for (auto index { 0 }; index < 8; ++index)
            check (zone (preset, side, index).getSample ().isEmpty () && zone (preset, side, index).getId () == index + 1, "Clear all eight slots including hidden orphan R zones");
    std::cout << "Paired zone copy/continue/insert/paste/flip/explode/clear passed\n";
}
