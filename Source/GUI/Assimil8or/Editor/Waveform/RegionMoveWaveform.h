#pragma once

#include "oolib/GUI/InteractiveWaveform.h"
#include "oolib/GUI/MarkerOverlay.h"

class RegionMarkerOverlay : public MarkerOverlay
{
public:
    bool hitTest (int x, int y) override
    {
        // MarkerOverlay's custom hit test does not consult Component's click
        // interception flags. Honour them when routing move-mode gestures.
        return isEnabled () && juce::Component::hitTest (x, y) && MarkerOverlay::hitTest (x, y);
    }
};

// Application-specific editing gestures; the shared oolib navigation stays intact.
class RegionMoveWaveform : public InteractiveWaveform
{
public:
    std::function<void ()> onDoubleClick;
    std::function<bool ()> onBeginRegionMove;
    std::function<void (double)> onMoveRegion;

    void setMovingRegion (bool enabled)
    {
        cancelDrag ();
        movingRegion = enabled;
        setMouseCursor (enabled ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void cancelDrag () { drag = Drag::none; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        cancelDrag ();
        if (movingRegion && e.mods.isLeftButtonDown ())
        {
            if (isEnabled () && getNumSamples () > 0 && onBeginRegionMove && onBeginRegionMove ())
            {
                drag = Drag::region;
                lastX = e.position.x;
                sampleDelta = 0.0;
                samplesPerPixel = getSamplesPerPixel ();
            }
            return;
        }
        drag = Drag::view;
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
        else if (drag == Drag::view)
            InteractiveWaveform::mouseDrag (e);
    }

    void mouseUp (const juce::MouseEvent&) override { cancelDrag (); }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        // Do not change the mapping underneath an active region drag.
        if (drag != Drag::region) InteractiveWaveform::mouseWheelMove (e, wheel);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        cancelDrag ();
        if (onDoubleClick) onDoubleClick ();
    }

private:
    enum class Drag { none, view, region };
    Drag drag { Drag::none };
    bool movingRegion { false };
    float lastX { 0.0f };
    double sampleDelta { 0.0 }, samplesPerPixel { 1.0 };
};
