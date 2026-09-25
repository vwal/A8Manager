#include "WaveformDisplay.h"
#include <cmath>
#include "../../../ModernTheme.h"
#include "../../../../SystemServices.h"
#include "oolib/Properties/RuntimeRootProperties.h"

namespace
{
    // The Assimil8or's own loop can never be shorter than this.
    constexpr juce::int64 kMinLoopLength { 4 };
}

WaveformDisplay::WaveformDisplay ()
{
    setupColours ();

    // Samples is what the module itself deals in, so it leads and is the
    // default; minutes:seconds is offered from the timeline's right-click menu.
    // Beats:bars is not on the list - there is no tempo here for it to mean
    // anything - which is why that menu has only two entries.
    timeline.setAvailableUnits ({ TimelineComponent::Unit::samples, TimelineComponent::Unit::timeMinutesSeconds });
    timeline.setUnit (TimelineComponent::Unit::samples);
    // Marker drag labels are formatted by the timeline, so they follow its unit.
    timeline.onUnitChanged = [this] (TimelineComponent::Unit) { markerOverlay.repaint (); };
    addAndMakeVisible (timeline);

    // Use a single high-contrast peak trace rather than a separate RMS layer.
    waveform.setRmsVisible (false);
    waveform.onViewChanged = [this] () { publishView (); };
    waveform.onDoubleClick = [this] ()
    {
        waveform.setVerticalZoom (1.0f);
        waveform.zoomToFit ();
        publishView ();
    };
    addAndMakeVisible (waveform);

    markerOverlay.setWaveformView (&waveform);
    markerOverlay.constrainPosition = [this] (int markerIndex, double proposedPosition) { return constrainMarker (markerIndex, proposedPosition); };
    markerOverlay.onMarkerMoved = [this] (int markerIndex) { markerMoved (markerIndex); };
    markerOverlay.formatPosition = [this] (double sample) { return timeline.formatSamplePosition (sample); };
    addAndMakeVisible (markerOverlay);

    setupMarkers ();
    editMode.addItem ("Edit edges", 1);
    editMode.addItem ("Move zone", 2);
    editMode.addItem ("Move loop", 3);
    editMode.setSelectedId (1, juce::dontSendNotification);
    editMode.setTooltip ("Edit edges: drag individual markers; drag the waveform to pan. "
                         "Move zone / Move loop: left-drag the waveform to move the white / orange pair without changing its length. "
                         "Hold Shift for finer movement. The other pair stays put. Use the scrollbar to pan.");
    editMode.onChange = [this] () { updateEditMode (); };
    addAndMakeVisible (editMode);
    waveform.onBeginRegionMove = [this] ()
    {
        movingRegion = hasSample () && editMode.getSelectedId () != 1
            ? RegionMove::capture (zoneProperties, editMode.getSelectedId () == 2 ? RegionMove::Target::sample : RegionMove::Target::loop, getSampleLength ())
            : std::nullopt;
        return movingRegion.has_value ();
    };
    waveform.onMoveRegion = [this] (double delta)
    {
        if (hasSample () && movingRegion && movingRegion->fileLength == getSampleLength ())
            RegionMove::apply (zoneProperties, *movingRegion, delta);
    };
    for (auto* button : { &zoomIn, &zoomOut, &fit, &zone, &loop })
        addAndMakeVisible (button);
    zoomIn.setTooltip ("Zoom in around the centre. Scroll over the waveform to zoom at the pointer.");
    zoomOut.setTooltip ("Zoom out");
    fit.setTooltip ("Show the complete sample (also double-click the waveform)");
    zone.setTooltip ("Fit the current sample start/end region");
    loop.setTooltip ("Fit the current loop region");
    zoomIn.onClick = [this] () { waveform.zoomByAroundX (0.5, waveform.getWidth () * 0.5f); publishView (); };
    zoomOut.onClick = [this] () { waveform.zoomByAroundX (2.0, waveform.getWidth () * 0.5f); publishView (); };
    fit.onClick = [this] () { waveform.setVerticalZoom (1.0f); waveform.zoomToFit (); publishView (); };
    zone.onClick = [this] () { focusZone (); };
    loop.onClick = [this] ()
    {
        if (! hasSample ()) return;
        const auto start { zoneProperties.getLoopStart ().value_or (0) };
        focusRange (static_cast<double> (start), start + zoneProperties.getLoopLength ().value_or (getSampleLength () - start));
    };
    zoomInfo.setFont (juce::FontOptions (12.0f));
    zoomInfo.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (zoomInfo);
    auditionRateLabel.setText ("Audition speed", juce::dontSendNotification);
    auditionRateLabel.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (auditionRateLabel);
    auditionRateSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    auditionRateSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, 22);
    // Equal travel per octave, with a full-precision editable multiplier.
    auditionRateSlider.setNormalisableRange ({ AudioPlayerProperties::minAuditionRate, AudioPlayerProperties::maxAuditionRate,
        [] (double start, double end, double value) { return start * std::pow (end / start, value); },
        [] (double start, double end, double value) { return std::log (value / start) / std::log (end / start); } });
    auditionRateSlider.textFromValueFunction = [] (double value)
    {
        return juce::String (value, 4).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + "x";
    };
    auditionRateSlider.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue (); };
    auditionRateSlider.setValue (1.0, juce::dontSendNotification);
    auditionRateSlider.setDoubleClickReturnValue (true, 1.0);
    auditionRateSlider.setScrollWheelEnabled (false);
    auditionRateSlider.setColour (juce::Slider::thumbColourId, Theme::accent);
    auditionRateSlider.setColour (juce::Slider::trackColourId, Theme::accent.darker (0.4f));
    auditionRateSlider.setColour (juce::Slider::backgroundColourId, Theme::border);
    auditionRateSlider.setColour (juce::Slider::textBoxTextColourId, Theme::text);
    auditionRateSlider.setColour (juce::Slider::textBoxBackgroundColourId, Theme::field);
    auditionRateSlider.setColour (juce::Slider::textBoxOutlineColourId, Theme::border);
    auditionRateSlider.setTooltip ("Audition speed: 0.0625x to 4x. Drag or type; double-click the slider for 1x. "
                                   "With Keep pitch on, speed changes duration without transposing. "
                                   "Zone PITCH OFFSET is applied separately. With it off, speed and pitch are linked. "
                                   "Channel pitch/CV and other hardware processing are not simulated.");
    auditionRateLabel.setTooltip (auditionRateSlider.getTooltip ());
    auditionRateSlider.onValueChange = [this] ()
    {
        audioPlayerProperties.setAuditionRate (auditionRateSlider.getValue (), false);
    };
    addAndMakeVisible (auditionRateSlider);
    preservePitchButton.setToggleState (true, juce::dontSendNotification);
    preservePitchButton.setColour (juce::ToggleButton::textColourId, Theme::text);
    preservePitchButton.setColour (juce::ToggleButton::tickColourId, Theme::accent);
    preservePitchButton.setTooltip ("Time-stretched preview: keep pitch steady as speed changes, then apply the zone's PITCH OFFSET. "
                                    "Extreme rates and very short loops may produce artifacts. "
                                    "Turn off for ordinary sampler-style varispeed (not a full hardware emulation). "
                                    "Preview mode/speed are not saved into the preset.");
    preservePitchButton.onClick = [this] () { audioPlayerProperties.setPreservePitch (preservePitchButton.getToggleState (), false); };
    addAndMakeVisible (preservePitchButton);
    scrollbar.addListener (this);
    scrollbar.setAutoHide (false);
    addAndMakeVisible (scrollbar);
}

void WaveformDisplay::focusZone ()
{
    if (hasSample ())
        focusRange (static_cast<double> (zoneProperties.getSampleStart ().value_or (0)),
                    static_cast<double> (zoneProperties.getSampleEnd ().value_or (getSampleLength ())));
}

void WaveformDisplay::focusRange (double start, double end)
{
    const auto length { std::max (1.0, end - start) };
    waveform.setVisibleRange (std::max (0.0, start - length * 0.05), length * 1.1);
    publishView ();
}

void WaveformDisplay::scrollBarMoved (juce::ScrollBar*, double start)
{
    waveform.setVisibleRange (start, waveform.getSamplesPerPixel () * waveform.getWidth ());
    publishView ();
}

void WaveformDisplay::setupColours ()
{
    const auto backgroundColour { Theme::field };

    WaveformView::ColourScheme waveformColours;
    waveformColours.background = backgroundColour;
    waveformColours.centreLine = Theme::border;
    waveformColours.peak       = Theme::accent;
    waveformColours.rms        = Theme::accent.withAlpha (0.4f);
    waveformColours.sampleLine = Theme::accent;
    waveformColours.sampleDot  = Theme::text;
    waveform.setColourScheme (waveformColours);

    TimelineComponent::ColourScheme timelineColours;
    timelineColours.background = backgroundColour;
    timelineColours.majorTick  = Theme::muted;
    timelineColours.minorTick  = Theme::border;
    timelineColours.text       = Theme::muted;
    timeline.setColourScheme (timelineColours);
}

void WaveformDisplay::setupMarkers ()
{
    MarkerOverlay::Style style;
    style.colour        = juce::Colours::white;
    style.lineThickness = 1.0f;
    style.shape         = MarkerOverlay::HandleShape::rectangle;
    style.handleWidth   = 10.0f;
    style.handleHeight  = 10.0f;
    style.label         = MarkerOverlay::LabelVisibility::whileDragging;

    // In each pair the handles hang inwards, off the side of the line that faces
    // the region they bound, so which line a handle belongs to stays readable
    // when the two are close together.

    // Sample start / end: solid lines, handles along the top.
    auto sampleStartStyle { style };
    sampleStartStyle.placement = MarkerOverlay::HandlePlacement::top;
    sampleStartStyle.alignment = MarkerOverlay::HandleAlignment::rightOfLine;
    markerOverlay.addMarker ({ "Start", 0.0, sampleStartStyle });

    auto sampleEndStyle { sampleStartStyle };
    sampleEndStyle.alignment = MarkerOverlay::HandleAlignment::leftOfLine;
    markerOverlay.addMarker ({ "End", 0.0, sampleEndStyle });

    // Loop start / end: dashed lines, handles along the bottom.
    auto loopStartStyle { style };
    loopStartStyle.colour = juce::Colour (0xffffc879);
    loopStartStyle.placement = MarkerOverlay::HandlePlacement::bottom;
    loopStartStyle.alignment = MarkerOverlay::HandleAlignment::rightOfLine;
    loopStartStyle.dashed    = true;
    markerOverlay.addMarker ({ "Loop Start", 0.0, loopStartStyle });

    auto loopEndStyle { loopStartStyle };
    loopEndStyle.alignment = MarkerOverlay::HandleAlignment::leftOfLine;
    markerOverlay.addMarker ({ "Loop End", 0.0, loopEndStyle });
}

void WaveformDisplay::init (juce::ValueTree channelPropertiesVT, juce::ValueTree rootPropertiesVT)
{
    RuntimeRootProperties runtimeRootProperties (rootPropertiesVT, RuntimeRootProperties::WrapperType::client, RuntimeRootProperties::EnableCallbacks::no);
    channelProperties.wrap (channelPropertiesVT, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::yes);
    sampleManagerProperties.wrap (runtimeRootProperties.getValueTree (), SampleManagerProperties::WrapperType::client, SampleManagerProperties::EnableCallbacks::no);
    audioPlayerProperties.wrap (runtimeRootProperties.getValueTree (), AudioPlayerProperties::WrapperType::client, AudioPlayerProperties::EnableCallbacks::yes);
    audioPlayerProperties.onAuditionRateChange = [this] (double rate) { auditionRateSlider.setValue (rate, juce::dontSendNotification); };
    auditionRateSlider.setValue (audioPlayerProperties.getAuditionRate (), juce::dontSendNotification);
    audioPlayerProperties.onPreservePitchChange = [this] (bool preserve) { preservePitchButton.setToggleState (preserve, juce::dontSendNotification); };
    preservePitchButton.setToggleState (audioPlayerProperties.getPreservePitch (), juce::dontSendNotification);
    audioPlayerProperties.onPlaybackPositionChange = [this] (double position)
    {
        playheadSample = position;
        if (isShowing ()) repaint (waveform.getBounds ());
    };
    audioPlayerProperties.onSampleSourceChanged = [this] (std::tuple<int, int>)
    {
        playheadSample = -1.0;
        repaint (waveform.getBounds ());
    };
    audioPlayerProperties.onPlayStateChange = [this] (AudioPlayerProperties::PlayState state)
    {
        if (state == AudioPlayerProperties::PlayState::stop)
            playheadSample = -1.0;
        repaint (waveform.getBounds ());
    };

    SystemServices systemServices { runtimeRootProperties.getValueTree (), SystemServices::WrapperType::client, SystemServices::EnableCallbacks::yes };
    editManager = systemServices.getEditManager ();

    setZone (0);
}

void WaveformDisplay::setZone (int zoneIndex)
{
    waveform.cancelDrag ();
    movingRegion.reset ();
    zoneProperties.wrap (channelProperties.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::yes);
    zoneProperties.onSampleChange = [this] (juce::String) { waveform.cancelDrag (); movingRegion.reset (); repaint (); };
    zoneProperties.onSampleStartChange = [this] (std::optional<juce::int64>) { updateMarkerPositions (); };
    zoneProperties.onSampleEndChange = [this] (std::optional<juce::int64>) { updateMarkerPositions (); };
    zoneProperties.onLoopStartChange = [this] (std::optional<juce::int64>) { updateMarkerPositions (); };
    zoneProperties.onLoopLengthChange = [this] (std::optional<double>) { updateMarkerPositions (); };
    zoneProperties.onSideChange = [this] (int) { updateDisplayChannel (); };

    sampleProperties.wrap (sampleManagerProperties.getSamplePropertiesVT (channelProperties.getId () - 1, zoneIndex), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::yes);
    sampleProperties.onStatusChange = [this] (SampleStatus) { updateAudioSource (); };
    sampleProperties.onAudioBufferPtrChange = [this] (AudioBufferType*) { updateAudioSource (); };
    sampleProperties.onSampleRateChange = [this] (double newSampleRate) { timeline.setSampleRate (newSampleRate); };

    updateAudioSource ();
}

//==============================================================================
bool WaveformDisplay::hasSample ()
{
    return zoneProperties.isValid () && sampleProperties.isValid () && sampleProperties.getStatus () == SampleStatus::exists;
}

juce::int64 WaveformDisplay::getSampleLength ()
{
    return hasSample () ? sampleProperties.getLengthInSamples () : 0;
}

int WaveformDisplay::getDisplayChannel ()
{
    if (! hasSample ())
        return 0;

    const auto side { zoneProperties.getSide () };
    return side < sampleProperties.getNumChannels () ? side : 0;
}

//==============================================================================
void WaveformDisplay::updateAudioSource ()
{
    waveform.cancelDrag ();
    movingRegion.reset ();
    // The waveform holds the buffer without owning it, and the SampleManager
    // announces an unload by clearing the status before it clears the pointer.
    // Letting go of it first means nothing here can read a buffer that has gone
    // away, and it also makes the channel change below free (there is nothing
    // left to summarise) rather than a scan of the outgoing buffer.
    waveform.setAudioBuffer (nullptr);
    waveform.setDisplayChannel (getDisplayChannel ());
    waveform.setAudioBuffer (hasSample () ? sampleProperties.getAudioBufferPtr () : nullptr);

    if (hasSample ())
        timeline.setSampleRate (sampleProperties.getSampleRate ());

    markerOverlay.setVisible (hasSample ());
    updateEditMode ();
    updateMarkerPositions ();
    publishView ();
}

void WaveformDisplay::updateDisplayChannel ()
{
    waveform.setDisplayChannel (getDisplayChannel ());
}

void WaveformDisplay::updateMarkerPositions ()
{
    if (! hasSample ())
        return;

    const auto sampleLength { getSampleLength () };
    const auto sampleStart { zoneProperties.getSampleStart ().value_or (0) };
    const auto sampleEnd { zoneProperties.getSampleEnd ().value_or (sampleLength) };
    const auto loopStart { zoneProperties.getLoopStart ().value_or (0) };
    const auto loopLength { static_cast<juce::int64> (zoneProperties.getLoopLength ().value_or (static_cast<double> (sampleLength - loopStart))) };

    markerOverlay.setPosition (kSampleStart, static_cast<double> (sampleStart));
    markerOverlay.setPosition (kSampleEnd, static_cast<double> (sampleEnd));
    markerOverlay.setPosition (kLoopStart, static_cast<double> (loopStart));
    markerOverlay.setPosition (kLoopEnd, static_cast<double> (loopStart + loopLength));
}

// The timeline and the overlay both position by sample, so they have to be
// handed the waveform's view every time a gesture changes it.
void WaveformDisplay::publishView ()
{
    timeline.setView (waveform.getVisibleStartSample (), waveform.getSamplesPerPixel ());
    markerOverlay.repaint ();
    repaint (waveform.getBounds ());
    const auto length { static_cast<double> (getSampleLength ()) };
    const auto visible { std::min (length, waveform.getSamplesPerPixel () * waveform.getWidth ()) };
    scrollbar.setRangeLimits (0.0, std::max (1.0, length), juce::dontSendNotification);
    scrollbar.setCurrentRange (waveform.getVisibleStartSample (), visible, juce::dontSendNotification);
    for (auto* button : { &zoomIn, &zoomOut, &fit, &zone, &loop }) button->setEnabled (length > 0);
    editMode.setEnabled (length > 0 && isEnabled ());
    scrollbar.setEnabled (length > visible);
    zoomInfo.setText (length > 0 && visible > 0 ? "Zoom " + juce::String (length / visible, 1) + "x" : "No sample", juce::dontSendNotification);
}

//==============================================================================
// Where a dragged marker may go, relative to the others. The overlay applies the
// audio bounds itself afterwards.
double WaveformDisplay::constrainMarker (int markerIndex, double proposedPosition)
{
    if (! hasSample ())
        return proposedPosition;

    const auto sampleLength { getSampleLength () };
    const auto position { static_cast<juce::int64> (proposedPosition) };

    switch (markerIndex)
    {
        case kSampleStart:
        {
            const auto sampleEnd { zoneProperties.getSampleEnd ().value_or (sampleLength) };
            return static_cast<double> (std::clamp (position, juce::int64 { 0 }, std::max (juce::int64 { 0 }, sampleEnd - 1)));
        }
        case kSampleEnd:
        {
            const auto sampleStart { zoneProperties.getSampleStart ().value_or (0) };
            return static_cast<double> (std::clamp (position, std::min (sampleStart + 1, sampleLength), sampleLength));
        }
        case kLoopStart:
        {
            const auto maxLoopStart { editManager == nullptr ? sampleLength
                                                             : editManager->getMaxLoopStart (channelProperties.getId () - 1, zoneProperties.getId () - 1) };
            return static_cast<double> (std::clamp (position, juce::int64 { 0 }, std::max (juce::int64 { 0 }, maxLoopStart)));
        }
        case kLoopEnd:
        {
            const auto loopStart { zoneProperties.getLoopStart ().value_or (0) };
            return static_cast<double> (std::clamp (position, std::min (loopStart + kMinLoopLength, sampleLength), sampleLength));
        }
        default:
        {
            return proposedPosition;
        }
    }
}

// A marker was dragged; write it back to the zone. A property setter here comes
// back through the onXChange callbacks above and repositions every marker, which
// is how the ones that have to follow this one get moved.
void WaveformDisplay::markerMoved (int markerIndex)
{
    if (! hasSample ())
        return;

    const auto sampleLength { getSampleLength () };
    const auto position { static_cast<juce::int64> (markerOverlay.getPosition (markerIndex)) };

    switch (markerIndex)
    {
        case kSampleStart:
        {
            zoneProperties.setSampleStart (position == 0 ? -1 : position, true);
        }
        break;

        case kSampleEnd:
        {
            zoneProperties.setSampleEnd (position == sampleLength ? -1 : position, true);
        }
        break;

        case kLoopStart:
        {
            const auto originalLoopStart { zoneProperties.getLoopStart ().value_or (0) };
            zoneProperties.setLoopStart (position == 0 ? -1 : position, true);
            if (channelProperties.getLoopLengthIsEnd ())
            {
                // Loop Length is always stored as a length, even when it is being
                // shown as an end, so holding the end still means moving the
                // length by however far the start travelled.
                const auto lengthChangeAmount { static_cast<double> (originalLoopStart - position) };
                const auto newLoopLength { zoneProperties.getLoopLength ().value_or (static_cast<double> (sampleLength)) + lengthChangeAmount };
                zoneProperties.setLoopLength (newLoopLength == static_cast<double> (sampleLength) ? -1.0 : newLoopLength, true);
            }
        }
        break;

        case kLoopEnd:
        {
            const auto newLoopLength { static_cast<double> (position - zoneProperties.getLoopStart ().value_or (0)) };
            zoneProperties.setLoopLength (newLoopLength == static_cast<double> (sampleLength) ? -1.0 : newLoopLength, true);
        }
        break;

        default:
        break;
    }
}

//==============================================================================
// Disabling the display stops it being edited, but panning, zooming and the
// timeline's unit menu only change the view, so they stay live.
void WaveformDisplay::enablementChanged ()
{
    updateEditMode ();
}

void WaveformDisplay::updateEditMode ()
{
    movingRegion.reset ();
    const auto moveMode { editMode.getSelectedId () != 1 };
    waveform.setMovingRegion (moveMode);
    // In move mode even drags on a marker reach the region gesture beneath it.
    markerOverlay.setInterceptsMouseClicks (isEnabled () && ! moveMode, false);
    editMode.setEnabled (isEnabled () && hasSample ());
}

void WaveformDisplay::resized ()
{
    auto bounds { getLocalBounds ().reduced (1) };
    auto toolbar { bounds.removeFromTop (28).reduced (3, 2) };
    for (auto* button : { &zoomOut, &zoomIn, &fit, &zone, &loop })
    {
        button->setBounds (toolbar.removeFromLeft (button == &zoomIn || button == &zoomOut ? 28 : 46));
        toolbar.removeFromLeft (4);
    }
    editMode.setBounds (toolbar.removeFromLeft (116));
    toolbar.removeFromLeft (6);
    // Keep the controls usable in a narrow viewport without squeezing the speed
    // entry or hiding the new edit mode. Wider windows retain the single row.
    if (getWidth () < 840)
        toolbar = bounds.removeFromTop (28).reduced (3, 2);
    const auto compact { getWidth () < 700 };
    auditionRateLabel.setText (compact ? "Speed" : "Audition speed", juce::dontSendNotification);
    auditionRateLabel.setBounds (toolbar.removeFromLeft (compact ? 44 : 90));
    auditionRateSlider.setBounds (toolbar.removeFromLeft (juce::jlimit (110, 210, toolbar.getWidth () - 169)));
    toolbar.removeFromLeft (4);
    preservePitchButton.setBounds (toolbar.removeFromLeft (100));
    zoomInfo.setBounds (toolbar);
    scrollbar.setBounds (bounds.removeFromBottom (12));
    timeline.setBounds (bounds.removeFromTop (juce::jmin (kTimelineHeight, bounds.getHeight () / 3)));
    waveform.setBounds (bounds);
    markerOverlay.setBounds (bounds);
    publishView ();
}

void WaveformDisplay::paintOverChildren (juce::Graphics& g)
{
    g.setColour (juce::Colours::black);
    g.drawRect (getLocalBounds ());

    if (! hasSample () || playheadSample < 0.0 ||
        audioPlayerProperties.getPlayState () == AudioPlayerProperties::PlayState::stop)
        return;
    const auto [playingChannel, playingZone] { audioPlayerProperties.getSampleSource () };
    if (playingChannel != channelProperties.getId () - 1 || playingZone != zoneProperties.getId () - 1)
        return;

    // Use the same original-sample coordinate mapping as the editable markers.
    // This indicator never intercepts a drag or changes the user's zoom/pan.
    const auto localX { waveform.sampleToX (playheadSample) };
    if (localX < 0.0f || localX >= waveform.getWidth ())
        return;
    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (waveform.getBounds ());
    const auto x { waveform.getX () + localX };
    const auto top { static_cast<float> (waveform.getY ()) };
    const auto bottom { static_cast<float> (waveform.getBottom ()) };
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.drawLine (x, top, x, bottom, 4.0f);
    g.setColour (juce::Colour (0xffffdf55));
    g.drawLine (x, top, x, bottom, 2.0f);
    juce::Path head;
    head.addTriangle (x - 5.0f, top, x + 5.0f, top, x, top + 7.0f);
    g.fillPath (head);
}
