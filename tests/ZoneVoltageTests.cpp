#include "GUI/Assimil8or/Editor/ChannelEditor.h"
#include <iostream>
#include <limits>
#include <stdexcept>

void testZoneVoltages ()
{
    auto check = [] (bool condition, const char* message)
    {
        if (! condition) throw std::runtime_error (message);
    };
    using namespace ZoneVoltageDisplay;
    check (accessValue (0.0, 5.0) == "+2.50", "First zone uses +5 V as its upper boundary");
    check (accessValue (-2.5, 0.0) == "-1.25", "Middle zone uses the previous lower boundary");
    check (accessValue (-5.0, -2.5) == "-3.75", "Last zone includes -5 V");
    check (accessValue (-5.0, 5.0) == "+0.00", "A single full-range zone targets zero");
    check (accessValue (0.0, 0.01) == "+0.005", "Half-centivolt midpoint must not round to a boundary");
    check (accessValue (-0.01, 0.0) == "-0.005", "Negative narrow range preserves precision");
    check (tabName (1, true, 0.0, 5.0) == "1\r+0.00\r(+2.50)", "Parenthesized target follows the unchanged lower boundary");
    check (tabName (4, false, -5.0, -5.0) == "4", "Empty zones show neither boundary nor target");
    for (auto zone { 0 }; zone < 8; ++zone)
    {
        const auto upper { 5.0 - zone * 1.25 };
        const auto lower { upper - 1.25 };
        check (accessValue (lower, upper).getDoubleValue () == (upper + lower) * 0.5, "All eight balanced-zone midpoints");
    }
    for (auto count { 2 }; count <= 8; ++count)
        for (auto zone { 0 }; zone < count; ++zone)
        {
            const auto upper { 5.0 - zone * (10.0 / count) };
            const auto lower { zone == count - 1 ? -5.0 : upper - 10.0 / count };
            const auto target { accessValue (lower, upper) };
            check (target.isNotEmpty () && std::abs (target.getDoubleValue () - (lower + upper) * 0.5) <= (upper - lower) * 0.0005,
                   "Balanced thirds/sevenths must show a practical rounded midpoint, not a missing target");
        }
    for (const auto invalid : { 0.0, 1.0, std::numeric_limits<double>::infinity (), std::numeric_limits<double>::quiet_NaN () })
        check (accessValue (invalid, 0.0).isEmpty (), "Invalid/reversed/zero-width range has no target");
    check (accessValue (-6.0, 0.0).isEmpty () && accessValue (0.0, 6.0).isEmpty (), "Reject out-of-range boundaries");
    check (accessValue (0.0, 1.0e-10).isEmpty (), "Unrepresentably narrow range must not show a boundary target");
    check (tabName (2, true, 0.0, 0.0).endsWith ("(--)") && tooltip (2, true, 0.0, 0.0).contains ("No reliable access"),
           "Invalid ranges explain the missing access value");

    // The same property wrappers and computation used by ChannelEditor, with no
    // writes from formatting. Changing a boundary affects both adjacent targets.
    ZoneProperties first (ZoneProperties::create (1), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    ZoneProperties second (ZoneProperties::create (2), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::no);
    first.setMinVoltage (1.0, false);
    second.setMinVoltage (-2.5, false);
    const auto before { first.getValueTree ().createCopy () };
    check (accessValue (first.getMinVoltage (), 5.0) == "+3.00" &&
           accessValue (second.getMinVoltage (), first.getMinVoltage ()) == "-0.75", "Neighbour targets follow a changed boundary");
    check (first.getValueTree ().isEquivalentTo (before), "Access values must not change saved properties");

    // Render the actual tab look-and-feel at all common UI sizes.
    ZonesTabbedLookAndFeel look;
    juce::TabbedComponent tabs (juce::TabbedButtonBar::TabsAtLeft);
    tabs.setLookAndFeel (&look);
    tabs.setTabBarDepth (58);
    tabs.setBounds (0, 0, 236, 480);
    const double boundaries[] { 0.0, -2.5, -5.0 };
    for (auto zone { 0 }; zone < 8; ++zone)
        tabs.addTab (tabName (zone + 1, zone < 3, zone < 3 ? boundaries[zone] : -5.0,
                             zone == 0 ? 5.0 : zone < 4 ? boundaries[zone - 1] : -5.0), Theme::panel, nullptr, false);
    tabs.setCurrentTabIndex (2);
    for (const auto scale : { 1.0f, 1.5f, 2.0f })
    {
        auto* button { tabs.getTabbedButtonBar ().getTabButton (1) };
        const auto picture { button->createComponentSnapshot (button->getLocalBounds (), true, scale) };
        auto tealPixels { 0 };
        for (auto y { picture.getHeight () * 2 / 3 }; y < picture.getHeight () - 2; ++y)
            for (auto x { 2 }; x < picture.getWidth () - 2; ++x)
            {
                const auto pixel { picture.getPixelAt (x, y) };
                if (pixel.getRed () < 120 && pixel.getGreen () > 130 && pixel.getBlue () > 120) ++tealPixels;
            }
        check (tealPixels > 10, "Access text must be visible in the bottom third of each populated tab");
    }
    const auto artifactDirectory { juce::SystemStats::getEnvironmentVariable ("A8MANAGER_TEST_ARTIFACTS", {}) };
    if (artifactDirectory.isNotEmpty ())
    {
        const juce::File directory { artifactDirectory };
        directory.createDirectory ();
        auto stream { directory.getChildFile ("zone-access-150.png").createOutputStream () };
        check (stream != nullptr && stream->setPosition (0), "Open zone-access artifact");
        check (juce::PNGImageFormat ().writeImageToStream (tabs.createComponentSnapshot (tabs.getLocalBounds (), true, 1.5f), *stream), "Write zone-access artifact");
        check (stream->truncate ().wasOk (), "Truncate old artifact tail");
    }
    tabs.setLookAndFeel (nullptr);
    std::cout << "PASS: zone targets, narrow ranges, empty/invalid zones, unchanged boundaries, neighbour edits and scaled tab rendering\n";
}
