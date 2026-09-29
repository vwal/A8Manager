#include "LoopPointsView.h"
#include "../../../ModernTheme.h"
#include <cmath>

LoopPointsView::LoopPointsView ()
{
    setTooltip ("Loop join: the end of the selected region is on the left, its beginning on the right. "
                "Both traces share automatic visual gain; audio volume is unchanged. "
                "Right-click END or START for directional zero-crossing nudges and opposite-boundary matching.");
}

void LoopPointsView::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu () && onContextMenu)
        onContextMenu (event.position.x >= getWidth () * 0.5f);
}

void LoopPointsView::setAudioBuffer (juce::AudioBuffer<float>* theAudioBuffer)
{
    audioBuffer = theAudioBuffer;
    repaint ();
}

void LoopPointsView::setLoopPoints (juce::int64 theSampleOffset, juce::int64 theNumSamples, int theSide)
{
    sampleOffset = theSampleOffset;
    numSamples = theNumSamples;
    side = theSide;
    repaint ();
}

void LoopPointsView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::field);
    if (getWidth () < 8 || getHeight () < 24)
        return;

    const auto seam { getWidth () * 0.5f };
    const auto centreY { (getHeight () + 12.0f) * 0.5f };
    const auto halfHeight { (getHeight () - 16.0f) * 0.5f };
    const auto endColour { Theme::markerColour (2) };
    g.setColour (Theme::border);
    g.drawHorizontalLine (juce::roundToInt (centreY), 1.0f, getWidth () - 1.0f);

    // Validate before forming pointers; the end point is exclusive. The old
    // reverse trace read numSamples and numSamples + 1, past the selected range.
    if (audioBuffer != nullptr && side >= 0 && side < audioBuffer->getNumChannels () &&
        sampleOffset >= 0 && numSamples > 0 && sampleOffset <= audioBuffer->getNumSamples () &&
        numSamples <= audioBuffer->getNumSamples () - sampleOffset)
    {
        const auto count { static_cast<int> (std::min<juce::int64> (numSamples, getWidth () / 2 - 2)) };
        const auto* start { audioBuffer->getReadPointer (side, static_cast<int> (sampleOffset)) };
        const auto* tail { start + numSamples - count };
        auto finiteSample = [] (float value) { return std::isfinite (value) ? value : 0.0f; };
        auto peak { 0.0f };
        for (auto i { 0 }; i < count; ++i)
            peak = std::max ({ peak, std::abs (finiteSample (start[i])), std::abs (finiteSample (tail[i])) });
        // A single gain preserves the relative levels/DC offset at the join.
        // Do not amplify effectively silent numerical noise.
        const auto gain { peak > 1.0e-6f ? 0.85f / peak : 1.0f };
        auto drawTrace = [&] (const float* samples, float left, float right, juce::Colour colour)
        {
            juce::Path trace;
            for (auto i { 0 }; i < count; ++i)
            {
                const auto x { left + (right - left) * i / juce::jmax (1, count - 1) };
                const auto y { centreY - finiteSample (samples[i]) * gain * halfHeight };
                if (i == 0) trace.startNewSubPath (x, y);
                else trace.lineTo (x, y);
            }
            if (count == 1) trace.lineTo (right, centreY - finiteSample (samples[0]) * gain * halfHeight);
            g.setColour (colour);
            g.strokePath (trace, juce::PathStrokeType (1.5f));
        };
        drawTrace (tail, 2.0f, seam - 0.5f, endColour);
        drawTrace (start, seam + 0.5f, getWidth () - 2.0f, Theme::accent);
    }

    g.setColour (Theme::muted);
    g.drawVerticalLine (static_cast<int> (seam), 13.0f, getHeight () - 1.0f);
    g.setColour (Theme::border);
    g.drawRect (getLocalBounds ());
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (endColour);
    g.drawText ("END", 4, 1, getWidth () / 2 - 8, 12, juce::Justification::centredLeft);
    g.setColour (Theme::accent);
    g.drawText ("START", getWidth () / 2 + 4, 1, getWidth () / 2 - 8, 12, juce::Justification::centredRight);
}
