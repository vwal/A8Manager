#pragma once
#include "WaveformPresentation.h"
#include "../../../ModernTheme.h"

// App-local ruler: grouped frame counts without modifying shared oolib code.
class WaveformRuler : public juce::Component
{
public:
    enum class Unit { samples, timeMinutesSeconds };
    std::function<void (Unit)> onUnitChanged;
    void setSampleRate (double rate) { sampleRate = rate; repaint (); }
    void setView (double start, double scale) { first = start; samplesPerPixel = scale; repaint (); }
    Unit getUnit () const { return unit; }
    void setUnit (Unit value) { unit = value; repaint (); if (onUnitChanged) onUnitChanged (unit); }
private:
    double first { 0.0 }, samplesPerPixel { 1.0 }, sampleRate { 44100.0 };
    Unit unit { Unit::samples };
    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! event.mods.isPopupMenu ()) return;
        juce::PopupMenu menu;
        menu.addItem (1, "Samples", true, unit == Unit::samples);
        menu.addItem (2, "Minutes:Seconds", true, unit == Unit::timeMinutesSeconds);
        menu.showMenuAsync (juce::PopupMenu::Options ().withTargetComponent (this), [safe = juce::Component::SafePointer<WaveformRuler> (this)] (int result)
        {
            if (safe != nullptr && result != 0) safe->setUnit (result == 1 ? Unit::samples : Unit::timeMinutesSeconds);
        });
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::field);
        if (samplesPerPixel <= 0.0 || ! std::isfinite (samplesPerPixel)) return;
        const auto divisor { unit == Unit::samples || sampleRate <= 0.0 ? 1.0 : sampleRate };
        const auto desired { std::max (unit == Unit::samples ? 1.0 : 0.000001, samplesPerPixel * 105.0 / divisor) };
        const auto magnitude { std::pow (10.0, std::floor (std::log10 (desired))) };
        const auto fraction { desired / magnitude };
        const auto step { (fraction <= 1.0 ? 1.0 : fraction <= 2.0 ? 2.0 : fraction <= 5.0 ? 5.0 : 10.0) * magnitude * divisor };
        const auto left { std::floor (first / step) * step };
        g.setFont (Theme::numericFont (11.0f));
        for (auto i { 0 }; i < getWidth () / 10 + 3; ++i)
        {
            const auto frame { left + i * step };
            const auto x { static_cast<float> ((frame - first) / samplesPerPixel) };
            if (x > getWidth () + 60) break;
            g.setColour (Theme::muted);
            g.drawVerticalLine (juce::roundToInt (x), getHeight () - 6.0f, static_cast<float> (getHeight ()));
            if (frame >= 0.0 && x >= -1.0f && x <= getWidth ())
            {
                const auto text { unit == Unit::samples ? WaveformPresentation::samples (frame) : WaveformPresentation::time (frame / divisor) };
                const auto width { std::min (100, getWidth ()) };
                g.drawFittedText (text, juce::jlimit (0, std::max (0, getWidth () - width), juce::roundToInt (x) - width / 2), 0, width, getHeight () - 5,
                                  juce::Justification::centred, 1);
            }
            g.setColour (Theme::border);
            for (auto minor { 1 }; minor < 5; ++minor)
                if (step / divisor >= 5.0 || unit != Unit::samples)
                    g.drawVerticalLine (juce::roundToInt (x + static_cast<float> (step / samplesPerPixel * minor / 5.0)), getHeight () - 3.0f, static_cast<float> (getHeight ()));
        }
    }
};
