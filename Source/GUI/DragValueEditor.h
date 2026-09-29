#pragma once
#include "oolib/GUI/CustomTextEditor.h"
#include "ModernTheme.h"
#include <cmath>
#include <limits>

// Plain vertical dragging uses oolib's range-aware acceleration and validation.
// Shift gestures use slower, unaccelerated increments for precise adjustment.
template <typename Base>
class DragValueEditor : public Base
{
public:
    DragValueEditor ()
    {
        this->setFont (Theme::numericFont (this->getFont ().getHeight ()));
        updateTextColour ();
    }
private:
    bool updatingTextColour { false };
    void updateTextColour ()
    {
        if (updatingTextColour) return;
        const juce::ScopedValueSetter<bool> updating (updatingTextColour, true);
        const auto previous { this->findColour (juce::TextEditor::textColourId) };
        const auto opaque { previous.withAlpha (1.0f) };
        // oolib's range validator and enablement handler write white/red into
        // TextEditor runs. Translate those semantic colours, without changing
        // its validation or the user's input, for both appearance palettes.
        const auto invalid { opaque == juce::Colours::red || opaque == juce::Colour (0xffff6666) || opaque == juce::Colour (0xffb21d30) };
        this->applyColourToAllText ((invalid ? Theme::error : Theme::text).withAlpha (this->isEnabled () ? 1.0f : 0.5f), true);
    }
    void colourChanged () override
    {
        juce::TextEditor::colourChanged ();
        updateTextColour ();
    }
    void lookAndFeelChanged () override
    {
        juce::TextEditor::lookAndFeelChanged ();
        updateTextColour ();
    }
    void enablementChanged () override
    {
        juce::TextEditor::enablementChanged ();
        updateTextColour ();
    }
    CustomComponentMouseHandler dragHandler;
    CustomComponentMouseHandler wheelHandler;
    bool dragging { false };
    bool selecting { false };
    bool fineDragging { false };
    bool fineScrolling { false };
    int lastDragY { 0 };
    double fineRemainder { 0.0 };
    // Four times slower than the normal handler's slowest drag, with no acceleration.
    static constexpr double finePixelsPerIncrement { 16.0 };

    DragRange range ()
    {
        const auto increment { this->getIncrementCallback != nullptr ? static_cast<double> (this->getIncrementCallback ()) : 1.0 };
        return { static_cast<double> (this->getMinValueCallback ()),
                 static_cast<double> (this->getMaxValueCallback ()), increment };
    }

    void startValueDrag (const juce::MouseEvent& event)
    {
        // Prime the existing handler; it only captures modifier-drag itself.
        const juce::MouseEvent dragEvent (event.source, event.position,
            event.mods.withFlags (juce::ModifierKeys::commandModifier), event.pressure,
            event.orientation, event.rotation, event.tiltX, event.tiltY, event.eventComponent,
            event.originalComponent, event.eventTime, event.getMouseDownPosition ().toFloat (),
            event.mouseDownTime, event.getNumberOfClicks (), false);
        dragHandler.mouseDown (dragEvent, nullptr, nullptr);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! this->isEnabled ())
            return;
        dragging = false;
        selecting = false;
        fineDragging = event.mods.isShiftDown ();
        lastDragY = event.y;
        fineRemainder = 0.0;
        if (event.mods.isPopupMenu ())
        {
            if (this->onFocusLost != nullptr) this->onFocusLost ();
            if (this->onPopupMenuCallback != nullptr) this->onPopupMenuCallback ();
            return;
        }
        juce::TextEditor::mouseDown (event);
        startValueDrag (event);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! this->isEnabled () || event.mods.isPopupMenu ()) return;
        if (! dragging && ! selecting)
        {
            const auto dx { std::abs (event.getDistanceFromDragStartX ()) };
            const auto dy { std::abs (event.getDistanceFromDragStartY ()) };
            dragging = dy >= 4 && (dy > dx || event.mods.isCommandDown ());
            selecting = dx >= 4 && ! dragging;
            if (dragging && this->onFocusLost != nullptr) this->onFocusLost ();
        }
        if (dragging)
        {
            this->setHighlightedRegion ({});
            const auto fine { event.mods.isShiftDown () };
            const auto dragRange { range () };
            if (fine != fineDragging)
            {
                // Do not carry acceleration or fractional steps across modes.
                dragHandler = {};
                startValueDrag (event);
                fineRemainder = 0.0;
                fineDragging = fine;
            }
            if (fine)
            {
                fineRemainder += (lastDragY - event.y) / finePixelsPerIncrement;
                const auto steps { std::trunc (fineRemainder) };
                fineRemainder -= steps;
                if (steps != 0.0 && this->onDragCallback != nullptr)
                    this->onDragCallback (steps * dragRange.increment);
            }
            else
                dragHandler.mouseDrag (event, dragRange, this->onDragCallback);
            lastDragY = event.y;
        }
        else
            juce::TextEditor::mouseDrag (event);
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        dragHandler.mouseUp (event);
        if (! dragging) juce::TextEditor::mouseUp (event);
        dragging = false;
    }

    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        // Scrolling the workspace must not accidentally change a value.
        if (this->isEnabled () && event.mods.isCommandDown ())
        {
            const auto fine { event.mods.isShiftDown () };
            if (fine != fineScrolling)
            {
                wheelHandler = {};
                fineScrolling = fine;
            }
            if (fine)
            {
                // macOS/mouse drivers can map Shift+vertical scrolling to X.
                // Prefer Y if both axes are present, so each event nudges once.
                const auto delta { std::abs (wheel.deltaY) >= std::numeric_limits<float>::epsilon () ? wheel.deltaY : wheel.deltaX };
                if (std::abs (delta) >= std::numeric_limits<float>::epsilon () && this->onDragCallback != nullptr)
                {
                    const auto direction { (delta > 0.0f ? 1.0 : -1.0) * (wheel.isReversed ? -1.0 : 1.0) };
                    this->onDragCallback (direction * range ().increment);
                }
            }
            else
                wheelHandler.mouseWheelMove (event, wheel, range (), this->onDragCallback);
        }
        else if (auto* parent = this->getParentComponent ())
            parent->mouseWheelMove (event.getEventRelativeTo (parent), wheel);
    }
};

using DragValueEditorInt = DragValueEditor<CustomTextEditorInt>;
using DragValueEditorInt64 = DragValueEditor<CustomTextEditorInt64>;
using DragValueEditorDouble = DragValueEditor<CustomTextEditorDouble>;
