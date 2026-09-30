#include "LoopPointsView.h"
#include "../../../ModernTheme.h"
#include "../Waveform/WaveformPresentation.h"
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

void LoopPointsView::setLoopPoints (juce::int64 theSampleOffset, double theNumSamples, int theSide, bool theLoopSelected)
{
    sampleOffset = theSampleOffset;
    numSamples = theNumSamples;
    side = theSide;
    loopSelected = theLoopSelected;
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
    const auto endColour { WaveformPresentation::markerColours[loopSelected ? 3 : 1] };
    const auto startColour { WaveformPresentation::markerColours[loopSelected ? 2 : 0] };
    g.setColour (Theme::border);
    g.drawHorizontalLine (juce::roundToInt (centreY), 1.0f, getWidth () - 1.0f);

    // Validate before forming pointers; the end point is exclusive. The old
    // reverse trace read numSamples and numSamples + 1, past the selected range.
    if (audioBuffer != nullptr && side >= 0 && side < audioBuffer->getNumChannels () &&
        sampleOffset >= 0 && std::isfinite (numSamples) && numSamples >= (loopSelected ? 4.0 : 1.0) && sampleOffset <= audioBuffer->getNumSamples () &&
        numSamples <= audioBuffer->getNumSamples () - sampleOffset)
    {
        const auto count { static_cast<int> (std::min (numSamples, static_cast<double> (getWidth () / 2 - 2))) };
        const auto* start { audioBuffer->getReadPointer (side, static_cast<int> (sampleOffset)) };
        auto finiteSample = [] (float value) { return std::isfinite (value) ? value : 0.0f; };
        auto sampleAt = [&] (double position)
        {
            const auto first { static_cast<int> (std::floor (position)) };
            const auto amount { static_cast<float> (position - first) };
            if (amount == 0.0f) return finiteSample (start[first]);
            const auto second { std::min (first + 1, audioBuffer->getNumSamples () - static_cast<int> (sampleOffset) - 1) };
            return finiteSample (start[first]) * (1.0f - amount) + finiteSample (start[second]) * amount;
        };
        const auto tail { numSamples - count };
        auto peak { 0.0f };
        for (auto i { 0 }; i < count; ++i)
            peak = std::max ({ peak, std::abs (sampleAt (i)), std::abs (sampleAt (tail + i)) });
        // A single gain preserves the relative levels/DC offset at the join.
        // Do not amplify effectively silent numerical noise.
        const auto gain { peak > 1.0e-6f ? 0.85f / peak : 1.0f };
        auto drawTrace = [&] (double offset, float left, float right, juce::Colour colour)
        {
            juce::Path trace;
            for (auto i { 0 }; i < count; ++i)
            {
                const auto x { left + (right - left) * i / juce::jmax (1, count - 1) };
                const auto y { centreY - sampleAt (offset + i) * gain * halfHeight };
                if (i == 0) trace.startNewSubPath (x, y);
                else trace.lineTo (x, y);
            }
            if (count == 1) trace.lineTo (right, centreY - sampleAt (offset) * gain * halfHeight);
            g.setColour (colour);
            g.strokePath (trace, juce::PathStrokeType (1.5f));
        };
        drawTrace (tail, 2.0f, seam - 0.5f, endColour);
        drawTrace (0.0, seam + 0.5f, getWidth () - 2.0f, startColour);
    }

    g.setColour (Theme::muted);
    g.drawVerticalLine (static_cast<int> (seam), 13.0f, getHeight () - 1.0f);
    g.setColour (Theme::border);
    g.drawRect (getLocalBounds ());
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (endColour);
    g.drawText ("END", 4, 1, getWidth () / 2 - 8, 12, juce::Justification::centredLeft);
    g.setColour (startColour);
    g.drawText ("START", getWidth () / 2 + 4, 1, getWidth () / 2 - 8, 12, juce::Justification::centredRight);
}
