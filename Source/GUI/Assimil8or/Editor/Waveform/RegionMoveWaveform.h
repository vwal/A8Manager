#pragma once
#include "oolib/GUI/InteractiveWaveform.h"

// Option/Alt moves a region only when the host accepts the mouse-down location.
// Plain left drag pans; right drag zooms; a stationary right click opens a menu.
class RegionMoveWaveform : public InteractiveWaveform
{
public:
    std::function<void ()> onDoubleClick, onFocus;
    std::function<bool (juce::Point<float>)> onBeginRegionMove;
    std::function<void (double)> onMoveRegion;
    std::function<void (juce::Point<float>)> onContextMenu;
    void cancelDrag () { drag = Drag::none; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        cancelDrag ();
        if (onFocus) onFocus ();
        down = e.position;
        if (e.mods.isAltDown () && e.mods.isLeftButtonDown () && ! e.mods.isPopupMenu ())
        {
            if (isEnabled () && getNumSamples () > 0 && onBeginRegionMove && onBeginRegionMove (e.position))
            {
                drag = Drag::region;
                lastX = e.position.x;
                sampleDelta = 0.0;
                samplesPerPixel = getSamplesPerPixel ();
            }
            return;
        }
        drag = e.mods.isPopupMenu () ? Drag::menu : Drag::view;
        InteractiveWaveform::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag == Drag::region)
        {
            if (! isEnabled ()) { cancelDrag (); return; }
            sampleDelta += (e.position.x - lastX) * samplesPerPixel * (e.mods.isShiftDown () ? 0.1 : 1.0);
            lastX = e.position.x;
            if (onMoveRegion) onMoveRegion (sampleDelta);
        }
        else
        {
            if (drag == Drag::menu && e.position.getDistanceFrom (down) >= 4.0f) drag = Drag::view;
            if (drag == Drag::view) InteractiveWaveform::mouseDrag (e);
        }
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        const auto showMenu { drag == Drag::menu };
        cancelDrag ();
        if (showMenu && onContextMenu) onContextMenu (down);
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (drag != Drag::region) InteractiveWaveform::mouseWheelMove (e, wheel);
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        cancelDrag ();
        if (! e.mods.isAltDown () && ! e.mods.isPopupMenu () && onDoubleClick) onDoubleClick ();
    }
private:
    enum class Drag { none, view, menu, region };
    Drag drag { Drag::none };
    juce::Point<float> down;
    float lastX { 0.0f };
    double sampleDelta { 0.0 }, samplesPerPixel { 1.0 };
};
