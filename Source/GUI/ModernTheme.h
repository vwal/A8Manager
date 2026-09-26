#pragma once
#include <JuceHeader.h>

namespace Theme
{
    inline const juce::Colour background { 0xff141b25 };
    inline const juce::Colour panel { 0xff202a38 };
    inline const juce::Colour field { 0xff111923 };
    inline const juce::Colour border { 0xff354356 };
    inline const juce::Colour text { 0xffe4ecf5 };
    inline const juce::Colour muted { 0xffa8bacd };
    inline const juce::Colour accent { 0xff58dac2 };
}

class ModernLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ModernLookAndFeel ()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Theme::background);
        setColour (juce::Label::textColourId, Theme::text);
        setColour (juce::TextEditor::backgroundColourId, Theme::field);
        setColour (juce::TextEditor::textColourId, Theme::text);
        setColour (juce::TextEditor::outlineColourId, Theme::border);
        setColour (juce::TextEditor::focusedOutlineColourId, Theme::accent);
        setColour (juce::TextEditor::highlightColourId, Theme::accent.withAlpha (0.3f));
        setColour (juce::TextButton::buttonColourId, Theme::panel);
        setColour (juce::TextButton::buttonOnColourId, Theme::accent.darker (0.5f));
        setColour (juce::TextButton::textColourOffId, Theme::text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ComboBox::backgroundColourId, Theme::field);
        setColour (juce::ComboBox::textColourId, Theme::text);
        setColour (juce::ComboBox::outlineColourId, Theme::border);
        setColour (juce::ComboBox::arrowColourId, Theme::muted);
        setColour (juce::ListBox::backgroundColourId, Theme::field);
        setColour (juce::ListBox::textColourId, Theme::text);
        setColour (juce::ListBox::outlineColourId, Theme::border);
        setColour (juce::PopupMenu::backgroundColourId, Theme::panel);
        setColour (juce::PopupMenu::textColourId, Theme::text);
        setColour (juce::PopupMenu::headerTextColourId, Theme::accent);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accent.darker (0.6f));
        setColour (juce::ScrollBar::thumbColourId, Theme::border.brighter (0.3f));
        setColour (juce::TabbedButtonBar::frontTextColourId, Theme::accent);
        setColour (juce::TabbedButtonBar::tabTextColourId, Theme::muted);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                               bool over, bool down) override
    {
        auto fill { down ? Theme::accent.darker (0.6f) : (over ? colour.brighter (0.15f) : colour) };
        g.setColour (fill.withMultipliedAlpha (button.isEnabled () ? 1.0f : 0.4f));
        const auto bounds { button.getLocalBounds ().toFloat ().reduced (0.5f) };
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (button.hasKeyboardFocus (true) ? Theme::accent : Theme::border);
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    }

    void drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool over, bool) override
    {
        const auto selected { button.isFrontTab () };
        const auto bounds { button.getActiveArea ().toFloat ().reduced (2.0f, 1.0f) };
        g.setColour (selected ? Theme::panel : (over ? Theme::panel.brighter (0.1f) : Theme::background));
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (selected ? Theme::accent : Theme::muted);
        g.setFont (juce::FontOptions (14.0f, selected ? juce::Font::bold : juce::Font::plain));
        g.drawText (button.getButtonText (), bounds, juce::Justification::centred);
        if (selected) g.fillRect (bounds.withY (bounds.getBottom () - 2.0f).withHeight (2.0f));
    }
};
