#include "AREnvelopeComponent.h"
#include "../../../ModernTheme.h"

const auto kAnchorSize { 8.0 };
const auto kOffset { kAnchorSize / 2.0 };

AREnvelopeComponent::AREnvelopeComponent ()
{
    arEnvelopeProperties.wrap ({}, AREnvelopeProperties::WrapperType::owner, AREnvelopeProperties::EnableCallbacks::yes);
    arEnvelopeProperties.onAttackPercentChanged = [this] (double attackPercent) { attackPercentChanged (attackPercent); };
    arEnvelopeProperties.onReleasePercentChanged = [this] (double releasePercent) { releasePercentChanged (releasePercent); };

    attackAnchor.setAmplitude (1.0);
    attackAnchor.setMaxTime (0.5);
    releaseAnchor.setAmplitude (0.0);
    releaseAnchor.setMaxTime (0.5);
}

void AREnvelopeComponent::attackPercentChanged (double attackPercent)
{
    attackAnchor.setTime (attackPercent);
    recalcAnchorPositions ();
    repaint ();
}

void AREnvelopeComponent::releasePercentChanged (double releasePercent)
{
    releaseAnchor.setTime (releasePercent);
    recalcAnchorPositions ();
    repaint ();
}

void AREnvelopeComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::field);
    const auto opacity { isEnabled () ? 1.0f : 0.4f };
    g.setColour (Theme::border.withAlpha (0.5f));
    for (auto division { 1 }; division < 4; ++division)
    {
        g.drawHorizontalLine (getHeight () * division / 4, 1.0f, getWidth () - 1.0f);
        g.drawVerticalLine (getWidth () * division / 4, 1.0f, getHeight () - 1.0f);
    }
    auto point = [] (EnvelopeAnchor& anchor)
    {
        return juce::Point<float> { static_cast<float> (kOffset + anchor.getX ()), static_cast<float> (kOffset + anchor.getY ()) };
    };
    juce::Path curve;
    curve.startNewSubPath (point (startAnchor));
    curve.lineTo (point (attackAnchor));
    curve.lineTo (point (releaseAnchor));
    auto fill { curve };
    fill.closeSubPath ();
    g.setColour (Theme::accent.withAlpha (0.12f * opacity));
    g.fillPath (fill);
    g.setColour (Theme::accent.withMultipliedAlpha (opacity));
    g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));
    for (auto* anchor : { &attackAnchor, &releaseAnchor })
    {
        const auto bounds { juce::Rectangle<float> (8.0f, 8.0f).withCentre (point (*anchor)) };
        g.setColour ((anchor->getActive () ? Theme::text : Theme::accent).withMultipliedAlpha (opacity));
        g.fillEllipse (bounds);
        g.setColour (Theme::field);
        g.drawEllipse (bounds, 1.0f);
    }
    g.setColour (Theme::border);
    g.drawRect (getLocalBounds ());
}

void AREnvelopeComponent::recalcAnchorPositions ()
{
    startAnchor.setX (0);
    startAnchor.setY (editorHeight);

    attackAnchor.setX (editorWidth * attackAnchor.getTime ());
    attackAnchor.setY (editorHeight - (editorHeight * attackAnchor.getAmplitude ()));

    releaseAnchor.setX (attackAnchor.getX () + (editorWidth * releaseAnchor.getTime ()));
    releaseAnchor.setY (editorHeight - (editorHeight * releaseAnchor.getAmplitude ()));
}

void AREnvelopeComponent::resized ()
{
    editorWidth = std::max (1.0, getWidth () - kAnchorSize);
    editorHeight = std::max (1.0, getHeight () - kAnchorSize);
    recalcAnchorPositions ();
}

void AREnvelopeComponent::mouseMove (const juce::MouseEvent& e)
{
    if (! isEnabled ())
        return;
    auto isMouseOverAnchor = [&e] (EnvelopeAnchor& anchor) -> bool
    {
        return juce::Rectangle<int> (static_cast<int> (anchor.getX ()),
                                     static_cast<int> (anchor.getY ()),
                                     static_cast<int> (kAnchorSize),
                                     static_cast<int> (kAnchorSize)).contains (e.x, e.y);
    };
    EnvelopeAnchor* mouseOverAnchor { nullptr };
    if (isMouseOverAnchor (attackAnchor))
        mouseOverAnchor = &attackAnchor;
    else if (isMouseOverAnchor (releaseAnchor))
        mouseOverAnchor = &releaseAnchor;

    if (mouseOverAnchor != curActiveAnchor)
    {
        auto setActive = [this] (bool isActive)
        {
            if (curActiveAnchor != nullptr)
            {
                curActiveAnchor->setActive (isActive);
                if (isActive == false)
                    dragStartAnchorX = 0;
                else
                    dragStartAnchorX = static_cast<int> (curActiveAnchor->getX ());
            }
        };
        setActive (false);
        curActiveAnchor = mouseOverAnchor;
        setActive (true);

        repaint ();
    }
}

void AREnvelopeComponent::mouseExit (const juce::MouseEvent&)
{
    if (! isEnabled ())
        return;
    if (curActiveAnchor != nullptr)
    {
        curActiveAnchor->setActive (false);
        curActiveAnchor = nullptr;
        repaint ();
    }
}

void AREnvelopeComponent::mouseDown ([[maybe_unused]] const juce::MouseEvent& e)
{
    if (! isEnabled ())
        return;
    if (curActiveAnchor == nullptr)
        return;
}

void AREnvelopeComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! isEnabled ())
        return;
    if (curActiveAnchor == nullptr)
        return;

    const auto newX { dragStartAnchorX + e.getDistanceFromDragStartX () };
    const auto newTime { newX / editorWidth };
    if (curActiveAnchor == &attackAnchor)
    {
        if (newTime - startAnchor.getTime () != attackAnchor.getTime ())
        {
            const auto newAttackTime { std::fmin (std::fmax ((float) newTime - startAnchor.getTime (), 0.0), std::fmin (attackAnchor.getMaxTime (), 1.0 - releaseAnchor.getTime ())) };
            attackAnchor.setTime (newAttackTime);
            arEnvelopeProperties.setAttackPercent (newAttackTime, false);
        }
    }
    else
    {
        if (newTime - attackAnchor.getTime () != releaseAnchor.getTime ())
        {
            const auto newReleaseTime { std::fmin (std::fmax ((float) newTime - attackAnchor.getTime (), 0.0), std::fmin (releaseAnchor.getMaxTime (), 1.0 - attackAnchor.getTime ())) };
            releaseAnchor.setTime (newReleaseTime);
            arEnvelopeProperties.setReleasePercent (newReleaseTime, false);
        }
    }
    recalcAnchorPositions ();
    repaint ();
}
