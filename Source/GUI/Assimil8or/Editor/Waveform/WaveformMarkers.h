#pragma once
#include "RegionMoveWaveform.h"
#include "WaveformPresentation.h"
#include "oolib/GUI/MarkerOverlay.h"
#include "../../../ModernTheme.h"

class RegionMarkerOverlay : public MarkerOverlay
{
public:
    std::function<void (int)> onSelectMarker;
    std::function<void ()> onMarkerGestureEnded;
    std::function<juce::String (int)> labelText;
    void attach (RegionMoveWaveform* view) { waveform = view; setWaveformView (view); }
    void setLoopSelected (bool loop) { loopSelected = loop; repaint (); }
    void cancelDrag ()
    {
        markerGesture = forwarding = false;
        if (onMarkerGestureEnded) onMarkerGestureEnded ();
    }

    int markerAt (juce::Point<float> point) const
    {
        if (waveform == nullptr || waveform->getNumSamples () <= 0) return -1;
        auto result { -1 };
        auto nearest { std::numeric_limits<float>::max () };
        for (auto index { 0 }; index < getNumMarkers (); ++index)
        {
            const auto& style { getStyle (index) };
            if (style.shape == HandleShape::none) continue;
            const auto x { waveform->sampleToX (getPosition (index)) };
            const auto offset { style.alignment == HandleAlignment::rightOfLine ? 0.0f :
                                style.alignment == HandleAlignment::leftOfLine ? style.handleWidth : style.handleWidth * 0.5f };
            const juce::Rectangle<float> handle { x - offset, style.placement == HandlePlacement::top ? 0.0f : getHeight () - style.handleHeight,
                                                   style.handleWidth, style.handleHeight };
            const auto distance { std::abs (point.x - handle.getCentreX ()) };
            if (handle.expanded (3.0f).contains (point) && distance <= nearest) { nearest = distance; result = index; }
        }
        return result;
    }
    bool hitTest (int x, int y) override
    {
        return juce::Component::hitTest (x, y) && markerAt ({ static_cast<float> (x), static_cast<float> (y) }) >= 0;
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        cancelDrag ();
        forwarding = e.mods.isAltDown () || e.mods.isPopupMenu () || ! isEnabled ();
        if (waveform == nullptr) return;
        if (forwarding) { waveform->mouseDown (e.getEventRelativeTo (waveform)); return; }
        if (waveform->onFocus) waveform->onFocus ();
        MarkerOverlay::mouseDown (e);
        markerGesture = getDraggedMarker () >= 0;
        if (markerGesture && onSelectMarker) onSelectMarker (getDraggedMarker ());
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (forwarding && waveform != nullptr) waveform->mouseDrag (e.getEventRelativeTo (waveform));
        else if (markerGesture && isEnabled ()) MarkerOverlay::mouseDrag (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (forwarding && waveform != nullptr) waveform->mouseUp (e.getEventRelativeTo (waveform));
        MarkerOverlay::mouseUp (e);
        cancelDrag ();
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (waveform != nullptr) waveform->mouseWheelMove (e.getEventRelativeTo (waveform), wheel);
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (waveform != nullptr) waveform->mouseDoubleClick (e.getEventRelativeTo (waveform));
    }

    std::array<juce::Rectangle<int>, 4> labelBounds () const
    {
        std::array<juce::Rectangle<int>, 4> placed {};
        if (waveform == nullptr || ! labelText || getWidth () <= 4 || getHeight () < 30) return placed;
        const auto rowHeight { juce::jlimit (10, 18, (getHeight () - 24) / 4) };
        const auto lanes { std::max (1, (getHeight () - 24) / rowHeight) };
        const juce::Font font { juce::FontOptions (std::min (11.0f, rowHeight - 1.0f)) };
        for (auto marker { 0 }; marker < std::min (4, getNumMarkers ()); ++marker)
        {
            const auto x { waveform->sampleToX (getPosition (marker)) };
            if (x < 0.0f || x > getWidth ()) continue;
            const auto width { std::min (getWidth () - 4, juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, labelText (marker))) + 10) };
            const auto left { juce::jlimit (2, std::max (2, getWidth () - width - 2), juce::roundToInt (x) + (marker % 2 == 0 ? 5 : -width - 5)) };
            for (auto attempt { 0 }; attempt < lanes; ++attempt)
            {
                const auto lane { marker < 2 ? attempt : lanes - 1 - attempt };
                const juce::Rectangle<int> candidate { left, 12 + lane * rowHeight, width, rowHeight - 1 };
                auto collision { false };
                for (auto previous { 0 }; previous < marker; ++previous)
                    collision = collision || candidate.intersects (placed[static_cast<size_t> (previous)]);
                if (! collision) { placed[static_cast<size_t> (marker)] = candidate; break; }
            }
        }
        return placed;
    }
    void paint (juce::Graphics& g) override
    {
        if (waveform == nullptr || waveform->getNumSamples () <= 0) return;
        if (getNumMarkers () == 4)
        {
            const auto first { loopSelected ? 2 : 0 };
            const auto left { juce::jlimit (0.0f, static_cast<float> (getWidth ()), waveform->sampleToX (getPosition (first))) };
            const auto right { juce::jlimit (left, static_cast<float> (getWidth ()), waveform->sampleToX (getPosition (first + 1))) };
            // Dimming identifies only the selected audition region. A sample's
            // post-loop audio may still be reached on release or while scrubbing.
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRect (0.0f, 0.0f, left, static_cast<float> (getHeight ()));
            g.fillRect (right, 0.0f, getWidth () - right, static_cast<float> (getHeight ()));
        }
        MarkerOverlay::paint (g);
        const auto labels { labelBounds () };
        for (auto marker { 0 }; marker < 4; ++marker)
        {
            const auto bounds { labels[static_cast<size_t> (marker)] };
            if (bounds.isEmpty ()) continue;
            g.setColour (Theme::field.withAlpha (0.9f));
            g.fillRoundedRectangle (bounds.toFloat (), 2.0f);
            g.setColour (WaveformPresentation::markerColours[static_cast<size_t> (marker)]);
            g.setFont (juce::FontOptions (std::min (11.0f, static_cast<float> (bounds.getHeight ()))));
            g.drawFittedText (labelText (marker), bounds.reduced (4, 0), juce::Justification::centredLeft, 1);
        }
    }
private:
    RegionMoveWaveform* waveform { nullptr };
    bool loopSelected { false }, forwarding { false }, markerGesture { false };
};
