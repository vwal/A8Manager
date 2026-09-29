#include "GUI/ModernTheme.h"
#include "GUI/DragValueEditor.h"
#include "GUI/GuiProperties.h"
#include "GUI/Assimil8or/Editor/Waveform/WaveformPresentation.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void check (bool passed, const char* message) { if (! passed) throw std::runtime_error (message); }
    double luminance (juce::Colour colour)
    {
        auto linear = [] (double value) { return value <= 0.04045 ? value / 12.92 : std::pow ((value + 0.055) / 1.055, 2.4); };
        return 0.2126 * linear (colour.getFloatRed ()) + 0.7152 * linear (colour.getFloatGreen ()) + 0.0722 * linear (colour.getFloatBlue ());
    }
    double contrast (juce::Colour foreground, juce::Colour background)
    {
        const auto a { luminance (foreground) }, b { luminance (background) };
        return (std::max (a, b) + 0.05) / (std::min (a, b) + 0.05);
    }
}

void testAppearance ()
{
    ModernLookAndFeel look;
    ModernNoArrowComboBoxLookAndFeel compactLook;
    juce::Component root;
    root.setLookAndFeel (&look);
    struct Restore { juce::Component& root; ~Restore () { root.setLookAndFeel (nullptr); Theme::setAppearance (false); } } restore { root };
    juce::Label heading;
    root.addAndMakeVisible (heading);
    Theme::bindColour (heading, juce::Label::textColourId, [] { return Theme::accent; });
    DragValueEditorInt64 endpoint;
    root.addAndMakeVisible (endpoint);
    endpoint.getMinValueCallback = [] { return juce::int64 { 0 }; };
    endpoint.getMaxValueCallback = [] { return juce::int64 { 1000000 }; };
    endpoint.toStringCallback = [] (juce::int64 value) { return juce::String (value); };
    auto changes { 0 };
    endpoint.updateDataCallback = [&] (juce::int64) { ++changes; };
    endpoint.setValue (123456);
    for (const auto light : { false, true, false, true })
    {
        Theme::setAppearance (light);
        Theme::refreshComponentTree (root);
        check (Theme::isLight () == light, "Selected appearance updates immediately");
        check (heading.findColour (juce::Label::textColourId) == Theme::accent, "Explicit heading colour follows palette changes");
        check (compactLook.findColour (juce::ComboBox::backgroundColourId) == Theme::field, "Arrow-free CV fields share the appearance palette");
        check (look.findColour (juce::PopupMenu::backgroundColourId) == Theme::panel, "Popup menus share the appearance palette");
        check (endpoint.findColour (juce::TextEditor::textColourId) == Theme::text, "Numeric text follows light/dark appearance");
        check (endpoint.getText () == "123456" && changes == 1, "Appearance never edits parameter content or commits a value");
        check (contrast (Theme::text, Theme::field) >= 4.5 && contrast (Theme::muted, Theme::panel) >= 4.5,
               "Both palettes retain readable body and secondary text");
        for (size_t marker { 0 }; marker < 4; ++marker)
            check (contrast (WaveformPresentation::markerColours[marker], Theme::field) >= 4.5,
                   "All four marker colours remain readable on the waveform surface");
        endpoint.applyColourToAllText (juce::Colours::red, true);
        check (endpoint.findColour (juce::TextEditor::textColourId) == Theme::error, "Range validation remains visible in either palette");
        endpoint.applyColourToAllText (juce::Colours::white, true);
        check (endpoint.findColour (juce::TextEditor::textColourId) == Theme::text, "Valid numeric values never revert to white in light mode");
        endpoint.setEnabled (false);
        check (endpoint.findColour (juce::TextEditor::textColourId) == Theme::text.withAlpha (0.5f), "Read-only numeric fields retain palette and disabled alpha");
        endpoint.setEnabled (true);
    }
    const auto font { endpoint.getFont () };
    check (font.getTypefaceName () == juce::Font::getDefaultMonospacedFontName (), "Endpoint fields request the platform monospaced font");
    check (std::abs (juce::GlyphArrangement::getStringWidth (font, "111111") - juce::GlyphArrangement::getStringWidth (font, "888888")) < 0.1f,
           "Numeric glyph widths remain aligned");
    juce::Slider slider;
    std::unique_ptr<juce::Label> sliderField { look.createSliderTextBox (slider) };
    check (sliderField->getFont ().getTypefaceName () == juce::Font::getDefaultMonospacedFontName (), "Designer sliders use monospaced numeric entry too");

    GuiProperties defaults ({}, GuiProperties::WrapperType::owner, GuiProperties::EnableCallbacks::no);
    check (! defaults.getLightAppearance (), "New preferences preserve the existing dark default");
    defaults.setLightAppearance (true);
    const auto xml { defaults.getValueTree ().createXml () };
    GuiProperties restored (juce::ValueTree::fromXml (*xml), GuiProperties::WrapperType::owner, GuiProperties::EnableCallbacks::no);
    check (restored.getLightAppearance (), "Appearance survives settings serialization");
    juce::ValueTree legacy { GuiProperties::GuiTypeId };
    legacy.setProperty (GuiProperties::UiScalePropertyId, 1.5, nullptr);
    GuiProperties migrated (legacy, GuiProperties::WrapperType::owner, GuiProperties::EnableCallbacks::no);
    check (! migrated.getLightAppearance () && migrated.getUiScale () == 1.5, "Older settings gain a dark default without changing UI size");

    { juce::Label temporary; Theme::bindColour (temporary, juce::Label::textColourId, [] { return Theme::muted; }); }
    Theme::setAppearance (false); // A destroyed popup/control must not leave a live callback target.
    std::cout << "PASS: persistent appearance, live palette bindings, contrast, validation colours and monospaced numeric fields\n";
}
