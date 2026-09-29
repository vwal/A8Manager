#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <functional>
#include <vector>

namespace Theme
{
    inline bool lightAppearance { false };
    inline juce::Colour background { 0xff141b25 };
    inline juce::Colour panel { 0xff202a38 };
    inline juce::Colour field { 0xff111923 };
    inline juce::Colour border { 0xff354356 };
    inline juce::Colour text { 0xffe4ecf5 };
    inline juce::Colour muted { 0xffa8bacd };
    inline juce::Colour accent { 0xff58dac2 };
    inline juce::Colour warning { 0xffffc472 };
    inline juce::Colour error { 0xffff6666 };

    inline bool isLight () { return lightAppearance; }
    inline juce::Font numericFont (float height)
    {
        return juce::FontOptions (juce::Font::getDefaultMonospacedFontName (), height, juce::Font::plain);
    }
    inline juce::Colour markerColour (size_t index)
    {
        static const std::array<juce::Colour, 4> dark {
            juce::Colour (0xffff8585), juce::Colour (0xff8fcaff),
            juce::Colour (0xffffc879), juce::Colour (0xfff39db5) };
        static const std::array<juce::Colour, 4> light {
            juce::Colour (0xffb32f39), juce::Colour (0xff175fa1),
            juce::Colour (0xff965600), juce::Colour (0xffa33266) };
        return (isLight () ? light : dark)[index];
    }

    // Explicit per-component colours otherwise retain the palette from their
    // construction. Weak bindings keep them live without extending UI lifetime.
    struct ColourBinding
    {
        juce::Component::SafePointer<juce::Component> component;
        int colourId;
        std::function<juce::Colour ()> colour;
    };
    inline std::vector<ColourBinding> colourBindings;
    inline void bindColour (juce::Component& component, int colourId, std::function<juce::Colour ()> colour)
    {
        const auto initialColour { colour () };
        colourBindings.erase (std::remove_if (colourBindings.begin (), colourBindings.end (),
            [] (const auto& binding) { return binding.component == nullptr; }), colourBindings.end ());
        const auto found { std::find_if (colourBindings.begin (), colourBindings.end (), [&] (const auto& binding)
            { return binding.component.getComponent () == &component && binding.colourId == colourId; }) };
        if (found == colourBindings.end ()) colourBindings.push_back ({ &component, colourId, std::move (colour) });
        else found->colour = std::move (colour);
        // Do not retain a vector iterator across a component callback.
        component.setColour (colourId, initialColour);
    }
    inline void refreshComponentTree (juce::Component& root)
    {
        const auto refreshSurfaces = [] (auto&& self, juce::Component& component) -> void
        {
            if (auto* tabs { dynamic_cast<juce::TabbedComponent*> (&component) })
                for (auto i { 0 }; i < tabs->getNumTabs (); ++i) tabs->setTabBackgroundColour (i, panel);
            if (auto* window { dynamic_cast<juce::ResizableWindow*> (&component) })
                window->setBackgroundColour (background);
            // JUCE keeps colours inside existing text runs; a look-and-feel
            // notification alone only recreates the caret, not those runs.
            if (auto* editor { dynamic_cast<juce::TextEditor*> (&component) })
                editor->applyColourToAllText (editor->findColour (juce::TextEditor::textColourId), false);
            for (auto* child : component.getChildren ()) self (self, *child);
        };
        refreshSurfaces (refreshSurfaces, root);
        root.sendLookAndFeelChange ();
        root.repaint ();
    }
    void setAppearance (bool light);
}

class ModernLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ModernLookAndFeel ()
    {
        instances.push_back (this);
        updateColours ();
    }
    ~ModernLookAndFeel () override
    {
        instances.erase (std::remove (instances.begin (), instances.end (), this), instances.end ());
    }
    static void updateAllColours ()
    {
        for (auto* look : instances) look->updateColours ();
    }
    void updateColours ()
    {
        setColourScheme (Theme::isLight () ? getLightColourScheme () : getDarkColourScheme ());
        setColour (juce::ResizableWindow::backgroundColourId, Theme::background);
        setColour (juce::Label::textColourId, Theme::text);
        setColour (juce::TextEditor::backgroundColourId, Theme::field);
        setColour (juce::TextEditor::textColourId, Theme::text);
        setColour (juce::TextEditor::outlineColourId, Theme::border);
        setColour (juce::TextEditor::focusedOutlineColourId, Theme::accent);
        setColour (juce::TextEditor::highlightColourId, Theme::accent.withAlpha (0.3f));
        setColour (juce::TextEditor::highlightedTextColourId, Theme::text);
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
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::ScrollBar::thumbColourId, Theme::border.brighter (0.3f));
        setColour (juce::TabbedButtonBar::frontTextColourId, Theme::accent);
        setColour (juce::TabbedButtonBar::tabTextColourId, Theme::muted);
        setColour (juce::TabbedButtonBar::tabOutlineColourId, Theme::border);
        setColour (juce::ToggleButton::textColourId, Theme::text);
        setColour (juce::ToggleButton::tickColourId, Theme::accent);
        setColour (juce::ToggleButton::tickDisabledColourId, Theme::muted);
        setColour (juce::Slider::textBoxTextColourId, Theme::text);
        setColour (juce::Slider::textBoxBackgroundColourId, Theme::field);
        setColour (juce::Slider::textBoxOutlineColourId, Theme::border);
        setColour (juce::Slider::backgroundColourId, Theme::border);
        setColour (juce::Slider::thumbColourId, Theme::accent);
        setColour (juce::Slider::trackColourId, Theme::accent);
        setColour (juce::AlertWindow::backgroundColourId, Theme::panel);
        setColour (juce::AlertWindow::textColourId, Theme::text);
        setColour (juce::AlertWindow::outlineColourId, Theme::border);
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* label { juce::LookAndFeel_V4::createSliderTextBox (slider) };
        label->setFont (Theme::numericFont (label->getFont ().getHeight ()));
        return label;
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
private:
    static inline std::vector<ModernLookAndFeel*> instances;
};

// Keep the compact arrow-free fields, but inherit the same live palette as the
// rest of the app instead of the third-party widget's fixed LookAndFeel_V4.
class ModernNoArrowComboBoxLookAndFeel : public ModernLookAndFeel
{
public:
    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto bounds { juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height)) };
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);
    }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (0, 0, box.getWidth (), box.getHeight ());
        label.setFont (getComboBoxFont (box));
    }
};

inline void Theme::setAppearance (bool light)
{
    lightAppearance = light;
    background = juce::Colour (light ? 0xffe7edf3 : 0xff141b25);
    panel = juce::Colour (light ? 0xfff3f6f9 : 0xff202a38);
    field = juce::Colour (light ? 0xffffffff : 0xff111923);
    border = juce::Colour (light ? 0xff8999ab : 0xff354356);
    text = juce::Colour (light ? 0xff1b2a3b : 0xffe4ecf5);
    muted = juce::Colour (light ? 0xff4a6074 : 0xffa8bacd);
    accent = juce::Colour (light ? 0xff006f61 : 0xff58dac2);
    warning = juce::Colour (light ? 0xff8e5000 : 0xffffc472);
    error = juce::Colour (light ? 0xffb21d30 : 0xffff6666);
    ModernLookAndFeel::updateAllColours ();
    // A component colour callback may create or restyle a child, adding another
    // binding. Dispatch a snapshot so such re-entrancy cannot invalidate it.
    const auto bindings { colourBindings };
    for (const auto& binding : bindings)
        if (binding.component != nullptr) binding.component->setColour (binding.colourId, binding.colour ());
    auto& desktop { juce::Desktop::getInstance () };
    for (auto i { 0 }; i < desktop.getNumComponents (); ++i)
        if (auto* component { desktop.getComponent (i) }) refreshComponentTree (*component);
}
