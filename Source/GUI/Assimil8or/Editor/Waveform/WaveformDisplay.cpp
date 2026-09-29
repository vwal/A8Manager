#include "WaveformDisplay.h"
#include <cmath>
#include "../../../ModernTheme.h"
#include "../../../../Assimil8or/Audio/PlaybackPitch.h"
#include "oolib/Properties/RuntimeRootProperties.h"

namespace
{
    // The Assimil8or's own loop can never be shorter than this.
    constexpr juce::int64 kMinLoopLength { 4 };
}

WaveformDisplay::WaveformDisplay ()
{
    setupColours ();
    scheduleBoundaryMatch = [] (std::function<void ()> callback) { juce::MessageManager::callAsync (std::move (callback)); };
    confirmBoundaryMatch = [] (const juce::String& message, std::function<void (bool)> callback)
    {
        auto* alert { new juce::AlertWindow ("Move boundary?", message, juce::AlertWindow::QuestionIcon) };
        alert->addButton ("Yes", 1);
        // Enter and Escape both preserve the existing markers by default.
        alert->addButton ("No", 0, juce::KeyPress (juce::KeyPress::returnKey), juce::KeyPress (juce::KeyPress::escapeKey));
        alert->enterModalState (true, juce::ModalCallbackFunction::create (
            [callback = std::move (callback)] (int choice) { callback (choice == 1); }), true);
    };

    setWantsKeyboardFocus (true);
    timeline.setUnit (WaveformRuler::Unit::samples);
    addAndMakeVisible (timeline);

    // Use a single high-contrast peak trace rather than a separate RMS layer.
    waveform.setRmsVisible (false);
    waveform.onViewChanged = [this] () { publishView (); };
    waveform.onDoubleClick = [this] () { resetZoom (); };
    waveform.onFocus = [this] () { if (isShowing ()) grabKeyboardFocus (); };
    waveform.onContextMenu = [this] (juce::Point<float> point) { showWaveformMenu (waveform.xToSample (point.x)); };
    addAndMakeVisible (waveform);

    markerOverlay.attach (&waveform);
    markerOverlay.constrainPosition = [this] (int markerIndex, double proposedPosition) { return constrainMarker (markerIndex, proposedPosition); };
    markerOverlay.onMarkerMoved = [this] (int markerIndex) { markerMoved (markerIndex); };
    markerOverlay.onSelectMarker = [this] (int marker) { selectRegion (marker >= kLoopStart); };
    markerOverlay.labelText = [this] (int marker) { return markerLabel (marker); };
    addAndMakeVisible (markerOverlay);

    setupMarkers ();
    waveform.onBeginRegionMove = [this] (juce::Point<float> point) { return beginRegionMove (point); };
    waveform.onMoveRegion = [this] (double delta)
    {
        if (canEdit () && hasSample () && movingRegion && movingRegion->fileLength == getSampleLength ())
            RegionMove::apply (zoneProperties, *movingRegion, delta);
    };
    for (auto* button : std::array<juce::Button*, 6> { &expandButton, &menuButton, &zoomIn, &zoomOut, &zoomInfo, &simulationButton })
        addAndMakeVisible (button);
    simulationButton.setEnabled (false);
    simulationButton.setTooltip ("Trigger sample into loop simulation: play forward from Sample Start through any gap, then repeat between the loop markers. "
                                 "Stops with this button or the highlighted STOP in Zones. Requires an audio sample and a loop ending after Sample Start; "
                                 "does not change the preset's play or loop mode.");
    simulationButton.onClick = [this] () { triggerSimulation (); };
    zoomIn.setTooltip ("Zoom in around the centre. Scroll over the waveform to zoom at the pointer.");
    zoomOut.setTooltip ("Zoom out");
    expandButton.setTooltip ("Expand the waveform over the channel controls; click again or Escape to close. Zone switching stays available.");
    menuButton.setTooltip ("Waveform tools: direct zoom and marker jumps (keys 1-4), zero-crossing nudges, and boundary matching. Option/Alt-drag inside a region to move it without resizing.");
    expandButton.onClick = [this] () { if (onExpandRequested) onExpandRequested (); };
    menuButton.onClick = [this] () { showWaveformMenu (std::nullopt); };
    zoomIn.onClick = [this] () { waveform.zoomByAroundX (0.5, waveform.getWidth () * 0.5f); publishView (); };
    zoomOut.onClick = [this] () { waveform.zoomByAroundX (2.0, waveform.getWidth () * 0.5f); publishView (); };
    zoomInfo.onClick = [this] () { resetZoom (); };
    zoomInfo.setTooltip ("Reset zoom: fit the whole file horizontally and restore 100% waveform height (also double-click the waveform).");
    durationInfo.setFont (juce::FontOptions (11.0f));
    durationInfo.setBorderSize ({ 0, 3, 0, 3 });
    durationInfo.setTooltip ("File, sample region and loop lengths in minutes:seconds at channel PITCH + zone PITCH OFFSET, capped at the sampler's playback-rate limit. "
                             "Excludes audition speed and external CV. Marker timestamps remain source-file positions. "
                             "Gray stripes always mark a gap from Sample End to a later Loop Start. With hardware looping enabled they extend through Loop End. "
                             "This hardware loop-extent hint does not change which region the audition buttons play.");
    addAndMakeVisible (durationInfo);
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
    auditionRateSlider.setTooltip ("Audition speed: 0.0625x to 4x. Drag or type; double-click the slider for 1x. "
                                   "With Keep pitch on, speed changes duration without transposing. "
                                   "Channel PITCH + zone PITCH OFFSET always change both pitch and duration. With it off, speed and pitch are linked. "
                                   "External CV and other hardware processing are not simulated.");
    auditionRateLabel.setTooltip (auditionRateSlider.getTooltip ());
    auditionRateSlider.onValueChange = [this] ()
    {
        if (canEdit ()) audioPlayerProperties.setAuditionRate (auditionRateSlider.getValue (), false);
    };
    addAndMakeVisible (auditionRateSlider);
    preservePitchButton.setToggleState (true, juce::dontSendNotification);
    preservePitchButton.setTooltip ("Keep audition-speed changes from altering pitch. Channel PITCH + zone PITCH OFFSET still change pitch and duration as on the sampler. "
                                    "Extreme rates and very short loops may produce artifacts. "
                                    "Turn off for ordinary sampler-style varispeed (not a full hardware emulation). "
                                    "Preview mode/speed are not saved into the preset.");
    preservePitchButton.onClick = [this] ()
    {
        if (canEdit ()) audioPlayerProperties.setPreservePitch (preservePitchButton.getToggleState (), false);
    };
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

void WaveformDisplay::focusLoop ()
{
    if (hasSample ()) focusRange (markerPosition (kLoopStart), markerPosition (kLoopEnd));
}

void WaveformDisplay::resetZoom ()
{
    waveform.setVerticalZoom (1.0f);
    waveform.zoomToFit ();
    publishView ();
}

void WaveformDisplay::setExpanded (bool value)
{
    expanded = value;
    expandButton.setToggleState (expanded, juce::dontSendNotification);
    expandButton.setButtonText (expanded ? "Close expanded waveform" : "Expand waveform");
    waveform.cancelDrag ();
    markerOverlay.cancelDrag ();
    if (isShowing ()) grabKeyboardFocus ();
}

void WaveformDisplay::setLoopSelected (bool value)
{
    loopSelected = value;
    markerOverlay.setLoopSelected (value);
    updateDurations ();
}

void WaveformDisplay::setReadOnly (bool value)
{
    if (readOnly == value) return;
    readOnly = value;
    // Keep navigation/menus/expansion enabled, but make handle gestures pass
    // through to view navigation and cancel pending edits and confirmations.
    markerOverlay.setEnabled (! readOnly);
    auditionRateSlider.setEnabled (! readOnly);
    preservePitchButton.setEnabled (! readOnly);
    enablementChanged ();
}

void WaveformDisplay::selectRegion (bool value)
{
    if (! canEdit ()) return;
    setLoopSelected (value);
    if (onRegionSelected) onRegionSelected (value);
}

bool WaveformDisplay::beginRegionMove (juce::Point<float> point)
{
    movingRegion.reset ();
    if (! canEdit () || ! hasSample ()) return false;
    const auto handle { markerOverlay.markerAt (point) };
    auto targetLoop { loopSelected };
    if (handle >= 0) targetLoop = handle >= kLoopStart;
    else
    {
        const auto position { waveform.xToSample (point.x) };
        const auto inSample { position >= markerPosition (kSampleStart) && position <= markerPosition (kSampleEnd) };
        const auto inLoop { position >= markerPosition (kLoopStart) && position <= markerPosition (kLoopEnd) };
        if (! inSample && ! inLoop) return false;
        if (! inSample) targetLoop = true;
        if (! inLoop) targetLoop = false;
    }
    movingRegion = RegionMove::capture (zoneProperties, targetLoop ? RegionMove::Target::loop : RegionMove::Target::sample, getSampleLength ());
    if (movingRegion) selectRegion (targetLoop);
    return movingRegion.has_value ();
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

}

void WaveformDisplay::lookAndFeelChanged ()
{
    setupColours ();
    // Colours only: do not reset marker positions, audition, zoom, or dragging.
    for (auto marker { 0 }; marker < markerOverlay.getNumMarkers (); ++marker)
    {
        auto style { markerOverlay.getStyle (marker) };
        style.colour = WaveformPresentation::markerColours[static_cast<size_t> (marker)];
        markerOverlay.setStyle (marker, style);
    }
    repaint ();
}

void WaveformDisplay::setupMarkers ()
{
    MarkerOverlay::Style style;
    style.colour        = juce::Colours::white;
    style.lineThickness = 1.0f;
    style.shape         = MarkerOverlay::HandleShape::rectangle;
    style.handleWidth   = 10.0f;
    style.handleHeight  = 10.0f;
    style.label         = MarkerOverlay::LabelVisibility::never; // collision-aware labels are drawn by RegionMarkerOverlay

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
    for (auto marker { 0 }; marker < 4; ++marker)
    {
        auto coloured { markerOverlay.getStyle (marker) };
        coloured.colour = WaveformPresentation::markerColours[static_cast<size_t> (marker)];
        markerOverlay.setStyle (marker, coloured);
    }
}

void WaveformDisplay::init (juce::ValueTree channelPropertiesVT, juce::ValueTree rootPropertiesVT)
{
    RuntimeRootProperties runtimeRootProperties (rootPropertiesVT, RuntimeRootProperties::WrapperType::client, RuntimeRootProperties::EnableCallbacks::no);
    channelProperties.wrap (channelPropertiesVT, ChannelProperties::WrapperType::client, ChannelProperties::EnableCallbacks::yes);
    channelProperties.onLoopModeChange = [this] (int) { updateMarkerPositions (); };
    channelProperties.onLoopLengthIsEndChange = [this] (bool) { ++matchGeneration; };
    channelProperties.onPitchChange = [this] (double) { updateDurations (); };
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
        refreshSimulationControls ();
        repaint (waveform.getBounds ());
    };
    audioPlayerProperties.onPlayStateChange = [this] (AudioPlayerProperties::PlayState state)
    {
        if (state == AudioPlayerProperties::PlayState::stop)
            playheadSample = -1.0;
        refreshSimulationControls ();
        repaint (waveform.getBounds ());
    };
    audioPlayerProperties.onSimulationPhaseChange = [this] (AudioPlayerProperties::SimulationPhase)
    {
        refreshSimulationControls ();
    };

    setZone (0);
}

void WaveformDisplay::setZone (int zoneIndex)
{
    ++sourceGeneration;
    waveform.cancelDrag ();
    markerOverlay.cancelDrag ();
    movingRegion.reset ();
    zoneProperties.wrap (channelProperties.getZoneVT (zoneIndex), ZoneProperties::WrapperType::client, ZoneProperties::EnableCallbacks::yes);
    zoneProperties.onSampleChange = [this] (juce::String) { ++sourceGeneration; waveform.cancelDrag (); markerOverlay.cancelDrag (); movingRegion.reset (); repaint (); };
    zoneProperties.onSampleStartChange = [this] (std::optional<juce::int64>) { updateMarkerPositions (); };
    zoneProperties.onSampleEndChange = [this] (std::optional<juce::int64>) { updateMarkerPositions (); };
    zoneProperties.onLoopStartChange = [this] (std::optional<juce::int64>) { updateMarkerPositions (); };
    zoneProperties.onLoopLengthChange = [this] (std::optional<double>) { updateMarkerPositions (); };
    zoneProperties.onSideChange = [this] (int) { updateDisplayChannel (); };
    zoneProperties.onPitchOffsetChange = [this] (double) { updateDurations (); };

    sampleProperties.wrap (sampleManagerProperties.getSamplePropertiesVT (channelProperties.getId () - 1, zoneIndex), SampleProperties::WrapperType::client, SampleProperties::EnableCallbacks::yes);
    sampleProperties.onStatusChange = [this] (SampleStatus) { updateAudioSource (); };
    sampleProperties.onAudioBufferPtrChange = [this] (AudioBufferType*) { updateAudioSource (); };
    sampleProperties.onSampleRateChange = [this] (double newSampleRate) { ++matchGeneration; timeline.setSampleRate (newSampleRate); updateDurations (); };

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
    return side >= 0 && side < sampleProperties.getNumChannels () ? side : 0;
}

//==============================================================================
void WaveformDisplay::updateAudioSource ()
{
    ++sourceGeneration;
    waveform.cancelDrag ();
    markerOverlay.cancelDrag ();
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
    updateMarkerPositions ();
    publishView ();
}

void WaveformDisplay::updateDisplayChannel ()
{
    ++sourceGeneration;
    waveform.setDisplayChannel (getDisplayChannel ());
}

void WaveformDisplay::updateMarkerPositions ()
{
    ++matchGeneration;
    refreshSimulationControls ();
    if (! hasSample ())
    {
        markerOverlay.setLoopExtension ({});
        return;
    }

    const auto sampleLength { getSampleLength () };
    const auto sampleStart { zoneProperties.getSampleStart ().value_or (0) };
    const auto sampleEnd { zoneProperties.getSampleEnd ().value_or (sampleLength) };
    const auto loopStart { zoneProperties.getLoopStart ().value_or (0) };
    const auto loopLength { zoneProperties.getLoopLength ().value_or (static_cast<double> (sampleLength - loopStart)) };

    markerOverlay.setPosition (kSampleStart, static_cast<double> (sampleStart));
    markerOverlay.setPosition (kSampleEnd, static_cast<double> (sampleEnd));
    markerOverlay.setPosition (kLoopStart, static_cast<double> (loopStart));
    // oolib draws handles at whole frames. Keep the exact fractional endpoint
    // in the model/readouts and round only its handle representation.
    markerOverlay.setPosition (kLoopEnd, static_cast<double> (loopStart) + loopLength);
    const auto loopMode { channelProperties.getLoopMode () };
    juce::Range<double> extension;
    if (sampleLength > 0 && std::isfinite (loopLength))
    {
        const auto start { std::clamp (static_cast<double> (sampleEnd), 0.0, static_cast<double> (sampleLength)) };
        // A separated bridge is useful editing information even when the saved
        // hardware Loop mode is off (audition looping is a different control).
        const auto loopEnabled { loopMode == 1 || loopMode == 2 };
        const auto end { std::clamp (static_cast<double> (loopStart) + (loopEnabled ? loopLength : 0.0), start, static_cast<double> (sampleLength)) };
        extension = { start, end };
    }
    markerOverlay.setLoopExtension (extension);
    updateDurations ();
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
    for (auto* button : { &zoomIn, &zoomOut, &menuButton, &zoomInfo }) button->setEnabled (length > 0);
    scrollbar.setEnabled (length > visible);
    zoomInfo.setButtonText (length > 0 && visible > 0 ? juce::String (100.0 * length / visible, 0) + "%" : "No sample");
    updateDurations ();
    refreshSimulationControls ();
}

bool WaveformDisplay::isSimulatingThisZone ()
{
    return audioPlayerProperties.isValid () && channelProperties.isValid () && zoneProperties.isValid ()
        && audioPlayerProperties.getPlayState () == AudioPlayerProperties::PlayState::sampleIntoLoop
        && audioPlayerProperties.getSampleSource () == std::make_tuple (channelProperties.getId () - 1, zoneProperties.getId () - 1);
}

void WaveformDisplay::refreshSimulationControls ()
{
    const auto active { isSimulatingThisZone () };
    simulationButton.setButtonText (active ? "Stop simulation" : "Sample > Loop");
    simulationButton.setToggleState (active, juce::dontSendNotification);
    simulationButton.setEnabled (canEdit () && (active || (onTriggerSimulation && canTriggerSimulation && canTriggerSimulation ())));
}

void WaveformDisplay::triggerSimulation ()
{
    if (! canEdit ()) return;
    if (isSimulatingThisZone ())
        audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, true);
    else if (onTriggerSimulation && canTriggerSimulation && canTriggerSimulation ())
        onTriggerSimulation ();
    refreshSimulationControls ();
}

//==============================================================================
// Where a dragged marker may go, relative to the others. The overlay applies the
// audio bounds itself afterwards.
double WaveformDisplay::markerPosition (int marker)
{
    if (! hasSample ()) return 0.0;
    switch (marker)
    {
        case kSampleStart: return static_cast<double> (zoneProperties.getSampleStart ().value_or (0));
        case kSampleEnd: return static_cast<double> (zoneProperties.getSampleEnd ().value_or (getSampleLength ()));
        case kLoopStart: return static_cast<double> (zoneProperties.getLoopStart ().value_or (0));
        case kLoopEnd: return markerPosition (kLoopStart) + zoneProperties.getLoopLength ().value_or (getSampleLength () - markerPosition (kLoopStart));
        default: return 0.0;
    }
}

juce::String WaveformDisplay::markerLabel (int marker)
{
    const auto position { markerPosition (marker) };
    const juce::String names[] { "S START", "S END", "L START", "L END" };
    const auto rate { hasSample () ? sampleProperties.getSampleRate () : 0.0 };
    auto text { names[marker] + " " + WaveformPresentation::samples (position) + " (" + WaveformPresentation::duration (position, rate) + ")" };
    if (marker == kSampleEnd || marker == kLoopEnd)
        text += "  length " + WaveformPresentation::duration (position - markerPosition (marker - 1), rate,
            PlaybackPitch::effectiveSemitones (channelProperties.getPitch (), zoneProperties.getPitchOffset (), rate));
    return text;
}

void WaveformDisplay::updateDurations ()
{
    if (! hasSample ()) { durationInfo.setText ("No sample", juce::dontSendNotification); return; }
    const auto rate { sampleProperties.getSampleRate () };
    const auto pitch { PlaybackPitch::effectiveSemitones (channelProperties.getPitch (), zoneProperties.getPitchOffset (), rate) };
    auto time = [rate, pitch] (double frames) { return WaveformPresentation::duration (frames, rate, pitch); };
    durationInfo.setText ("File " + time (static_cast<double> (getSampleLength ())) + "  |  " +
        (loopSelected ? "Sample " : "SAMPLE ") + time (markerPosition (kSampleEnd) - markerPosition (kSampleStart)) + "  |  " +
        (loopSelected ? "LOOP " : "Loop ") + time (markerPosition (kLoopEnd) - markerPosition (kLoopStart)) +
        "  @ " + juce::String (pitch >= 0.0 ? "+" : "") + juce::String (pitch, 2) + " st", juce::dontSendNotification);
    markerOverlay.repaint ();
}

void WaveformDisplay::jumpToMarker (int marker)
{
    if (! hasSample ()) return;
    const auto span { waveform.getSamplesPerPixel () * waveform.getWidth () };
    waveform.setVisibleRange (markerPosition (marker) - span * 0.5, span);
    publishView ();
}

juce::PopupMenu WaveformDisplay::buildWaveformMenu (std::optional<double> clickedSample)
{
    const auto available { hasSample () };
    const auto editable { available && canEdit () };
    juce::PopupMenu menu, nudgeMenu, matchMenu;
    menu.addSectionHeader ("ZOOM");
    menu.addItem (1, "Reset Zoom", available);
    menu.addItem (2, "Zoom to Sample Markers", available);
    menu.addItem (3, "Zoom to Loop Markers", available);
    menu.addSeparator ();
    menu.addSectionHeader ("JUMP TO MARKER");
    for (auto marker { 0 }; marker < 4; ++marker)
    {
        const auto name { WaveformPresentation::markerNames[static_cast<size_t> (marker)] };
        juce::PopupMenu::Item jump { name };
        jump.itemID = 10 + marker;
        jump.isEnabled = available;
        jump.shortcutKeyDescription = juce::String (marker + 1);
        menu.addItem (std::move (jump));
        juce::PopupMenu direction;
        direction.addItem (20 + marker * 2, "Left <<", editable);
        direction.addItem (21 + marker * 2, "Right >>", editable);
        nudgeMenu.addSubMenu (name, direction, editable);
        juce::PopupMenu matchDirection;
        matchDirection.addItem (40 + marker * 2, "Left <<", editable);
        matchDirection.addItem (41 + marker * 2, "Right >>", editable);
        matchMenu.addSubMenu (name + " to " + (marker % 2 == 0 ? "End" : "Start"), matchDirection, editable);
    }
    if (clickedSample)
    {
        menu.addSeparator ();
        menu.addSectionHeader ("SET MARKER HERE");
        for (auto marker { 0 }; marker < 4; ++marker)
            menu.addItem (30 + marker, WaveformPresentation::markerNames[static_cast<size_t> (marker)], editable);
    }
    menu.addSeparator ();
    menu.addSubMenu ("Zero Crossing Nudge", nudgeMenu, editable);
    menu.addSubMenu ("Match Opposite Boundary", matchMenu, editable);
    menu.addSeparator ();
    menu.addSectionHeader ("AUDITION");
    const auto simulating { isSimulatingThisZone () };
    menu.addItem (50, simulating ? "Stop sample into loop simulation" : "Trigger sample into loop simulation",
                  canEdit () && (simulating || (onTriggerSimulation && canTriggerSimulation && canTriggerSimulation ())));
    return menu;
}

void WaveformDisplay::showWaveformMenu (std::optional<double> clickedSample)
{
    auto menu { buildWaveformMenu (clickedSample) };
    auto options { juce::PopupMenu::Options () };
    if (! clickedSample) options = options.withTargetComponent (&menuButton);
    menu.showMenuAsync (options, [safe = juce::Component::SafePointer<WaveformDisplay> (this), generation = sourceGeneration, editGeneration = matchGeneration, clickedSample] (int action)
    {
        // A zone/sample may change, or the component may disappear, while a
        // native asynchronous menu is open. Never edit a different source.
        if (safe != nullptr && safe->sourceGeneration == generation && safe->matchGeneration == editGeneration)
            safe->applyMenuAction (action, clickedSample);
    });
}

void WaveformDisplay::applyMenuAction (int action, std::optional<double> clickedSample)
{
    if (action == 1) resetZoom ();
    else if (action == 2) focusZone ();
    else if (action == 3) focusLoop ();
    else if (action >= 10 && action < 14) jumpToMarker (action - 10);
    else if (canEdit () && action >= 20 && action < 28) nudgeMarker ((action - 20) / 2, action % 2 != 0);
    else if (canEdit () && action >= 30 && action < 34 && clickedSample) setMarker (action - 30, *clickedSample);
    else if (canEdit () && action >= 40 && action < 48) matchMarker ((action - 40) / 2, action % 2 != 0);
    else if (action == 50) triggerSimulation ();
}

juce::PopupMenu WaveformDisplay::boundaryAdjustmentMenu (int marker)
{
    juce::PopupMenu menu;
    if (marker < kSampleStart || marker > kLoopEnd) return menu;
    // The mini join preview and numeric-field menus use the exact same async
    // search, >50 ms approval and stale-source protection as the main waveform.
    for (const auto matching : { false, true })
    {
        juce::PopupMenu directions;
        for (const auto right : { false, true })
        {
            juce::PopupMenu::Item item { right ? "Right >>" : "Left <<" };
            item.itemID = (matching ? 40 : 20) + marker * 2 + (right ? 1 : 0);
            item.isEnabled = canEdit () && hasSample ();
            item.action = [safe = juce::Component::SafePointer<WaveformDisplay> (this),
                           source = sourceGeneration, edit = matchGeneration, marker, right, matching] ()
            {
                if (safe != nullptr && safe->sourceGeneration == source && safe->matchGeneration == edit)
                    safe->beginBoundaryMove (marker, right, matching);
            };
            directions.addItem (std::move (item));
        }
        menu.addSubMenu (matching ? "Match Opposite Boundary" : "Zero Crossing Nudge", directions, canEdit () && hasSample ());
    }
    return menu;
}

void WaveformDisplay::nudgeMarker (int marker, bool right)
{
    beginBoundaryMove (marker, right, false);
}

void WaveformDisplay::matchMarker (int marker, bool right)
{
    beginBoundaryMove (marker, right, true);
}

struct WaveformDisplay::BoundaryMoveRequest
{
    unsigned int generation, source;
    const AudioBufferType* buffer;
    double rate, original;
    int marker, side;
    bool right, endMode, matching;
    std::optional<WaveformPresentation::BoundaryMatchSearch> search;
};

bool WaveformDisplay::isCurrentMove (const BoundaryMoveRequest& request)
{
    return canEdit () && hasSample () && matchGeneration == request.generation && sourceGeneration == request.source &&
        sampleProperties.getAudioBufferPtr () == request.buffer && sampleProperties.getSampleRate () == request.rate &&
        getDisplayChannel () == request.side && channelProperties.getLoopLengthIsEnd () == request.endMode;
}

void WaveformDisplay::beginBoundaryMove (int marker, bool right, bool matching)
{
    if (! hasSample () || ! canEdit () || marker < kSampleStart || marker > kLoopEnd) return;
    const auto* buffer { sampleProperties.getAudioBufferPtr () };
    if (buffer == nullptr) return;
    // Starting either command supersedes pending matches and nudge approvals,
    // even if this search finds nothing or the source metadata is unusable.
    const auto generation { ++matchGeneration };
    const auto rate { sampleProperties.getSampleRate () };
    if (! std::isfinite (rate) || rate <= 0.0) return;
    const auto endBoundary { marker == kSampleEnd || marker == kLoopEnd };
    const auto minimum { static_cast<juce::int64> (constrainMarker (marker, 0.0, matching)) };
    const auto maximum { static_cast<juce::int64> (constrainMarker (marker, static_cast<double> (getSampleLength ()), matching)) };
    const auto original { markerPosition (marker) };
    auto request { std::make_shared<BoundaryMoveRequest> (BoundaryMoveRequest {
        generation, sourceGeneration, buffer, rate, original, marker, getDisplayChannel (), right,
        channelProperties.getLoopLengthIsEnd (), matching, {} }) };
    if (matching)
    {
        request->search.emplace (*buffer, request->side, original,
            markerPosition (endBoundary ? marker - 1 : marker + 1), minimum, maximum, rate, endBoundary, right);
        continueBoundaryMatch (std::move (request));
    }
    else if (const auto crossing { WaveformPresentation::zeroCrossing (*buffer, request->side, original, minimum, maximum, right, endBoundary) })
        finishBoundaryMove (std::move (request), *crossing);
    else
        durationInfo.setText ("No zero crossing " + juce::String (right ? "to the right" : "to the left") + " within this marker's valid range.", juce::dontSendNotification);
}

void WaveformDisplay::continueBoundaryMatch (std::shared_ptr<BoundaryMoveRequest> request)
{
    // SampleManager mutates/unloads on this thread. Revalidate before every
    // bounded batch: no worker can retain or dereference a freed sample buffer.
    if (! isCurrentMove (*request) || ! request->search) return;
    auto safe = juce::Component::SafePointer<WaveformDisplay> (this);
    if (! request->search->advance (65536))
    {
        durationInfo.setText ("Searching for a boundary match to the " + juce::String (request->right ? "right" : "left") + "...", juce::dontSendNotification);
        scheduleBoundaryMatch ([safe, request] ()
        {
            if (safe != nullptr) safe->continueBoundaryMatch (request);
        });
        return;
    }
    const auto match { request->search->result () };
    if (! match)
    {
        durationInfo.setText ("No improving nearest target-level crossing to the " + juce::String (request->right ? "right" : "left") +
                              "; marker unchanged.", juce::dontSendNotification);
        return;
    }
    finishBoundaryMove (std::move (request), *match);
}

void WaveformDisplay::finishBoundaryMove (std::shared_ptr<BoundaryMoveRequest> request, juce::int64 position)
{
    if (! isCurrentMove (*request)) return;
    auto safe = juce::Component::SafePointer<WaveformDisplay> (this);
    auto apply = [safe, request, position] (bool approved)
    {
        if (safe == nullptr || ! safe->isCurrentMove (*request)) return;
        // A callback is single-use, including cancellation.
        ++safe->matchGeneration;
        if (! approved) { safe->updateDurations (); return; }
        safe->setMarker (request->marker, static_cast<double> (position), request->matching);
        safe->jumpToMarker (request->marker);
    };
    const auto distance { std::abs (static_cast<double> (position) - request->original) };
    if (distance <= request->rate * 0.05) apply (true);
    else
    {
        const auto milliseconds { distance * 1000.0 / request->rate };
        const auto explanation { ! request->matching && request->marker == kLoopStart && ! request->endMode
            ? "Loop End will move with Loop Start to preserve the loop length."
            : "The opposite boundary will stay fixed." };
        const auto message { juce::String (request->matching ? "The nearest boundary match for " : "The nearest zero crossing for ") +
            WaveformPresentation::markerNames[static_cast<size_t> (request->marker)] +
            " is " + juce::String (milliseconds, milliseconds < 100.0 ? 2 : 1) + " ms to the " +
            juce::String (request->right ? "right" : "left") + ".\n\nDo you want to proceed?\n" + explanation };
        confirmBoundaryMatch (message, std::move (apply));
    }
}

bool WaveformDisplay::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode () == juce::KeyPress::escapeKey && expanded)
    {
        if (onExpandRequested) onExpandRequested ();
        return true;
    }
    if (key.getModifiers ().isAnyModifierKeyDown ()) return false;
    const auto number { key.getKeyCode () - '1' };
    if (number < 0 || number > 3) return false;
    jumpToMarker (number);
    return true;
}

double WaveformDisplay::constrainMarker (int markerIndex, double proposedPosition, bool keepOppositeBoundary)
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
            const auto length { markerPosition (kLoopEnd) - markerPosition (kLoopStart) };
            const auto maxLoopStart { static_cast<juce::int64> (std::floor ((keepOppositeBoundary || channelProperties.getLoopLengthIsEnd ())
                ? markerPosition (kLoopEnd) - kMinLoopLength : sampleLength - length)) };
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
    setMarker (markerIndex, markerOverlay.getPosition (markerIndex));
}

void WaveformDisplay::setMarker (int markerIndex, double proposedPosition, bool keepOppositeBoundary)
{
    if (! hasSample () || ! canEdit () || ! std::isfinite (proposedPosition)) return;
    const auto sampleLength { getSampleLength () };
    const auto position { static_cast<juce::int64> (constrainMarker (markerIndex, std::round (std::clamp (proposedPosition, 0.0, static_cast<double> (sampleLength))), keepOppositeBoundary)) };
    selectRegion (markerIndex >= kLoopStart);

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
            const auto oldLength { zoneProperties.getLoopLength ().value_or (static_cast<double> (sampleLength - originalLoopStart)) };
            const auto length { (keepOppositeBoundary || channelProperties.getLoopLengthIsEnd ()) ? oldLength + originalLoopStart - position : oldLength };
            auto setStart = [&] () { zoneProperties.setLoopStart (position == 0 ? -1 : position, true); };
            auto setLength = [&] () { zoneProperties.setLoopLength (length, true); };
            if (position >= originalLoopStart) { setLength (); setStart (); }
            else { setStart (); setLength (); }
        }
        break;

        case kLoopEnd:
        {
            const auto loopStart { zoneProperties.getLoopStart ().value_or (0) };
            const auto newLoopLength { static_cast<double> (position - loopStart) };
            zoneProperties.setLoopLength (position == sampleLength && loopStart == 0 ? -1.0 : newLoopLength, true);
        }
        break;

        default:
        break;
    }
    updateMarkerPositions ();
}

//==============================================================================
// Cancel edits whenever enablement/read-only state changes. Stereo-right uses
// read-only rather than disabling the component, keeping navigation live.
void WaveformDisplay::enablementChanged ()
{
    ++matchGeneration;
    waveform.cancelDrag ();
    markerOverlay.cancelDrag ();
    movingRegion.reset ();
    refreshSimulationControls ();
}

void WaveformDisplay::resized ()
{
    auto bounds { getLocalBounds ().reduced (1) };
    auto toolbar { bounds.removeFromTop (28).reduced (3, 2) };
    for (auto* button : std::array<juce::Button*, 4> { &expandButton, &menuButton, &zoomOut, &zoomIn })
    {
        button->setBounds (toolbar.removeFromLeft (26));
        toolbar.removeFromLeft (4);
    }
    zoomInfo.setBounds (toolbar.removeFromLeft (76));
    toolbar.removeFromLeft (6);
    // In a short, narrow waveform, keep the existing single toolbar row so
    // marker labels still have room. The same action remains in both menus.
    const auto inlineSimulation { getWidth () >= 650 || getHeight () >= 180 };
    simulationButton.setVisible (inlineSimulation);
    simulationButton.setBounds (inlineSimulation ? toolbar.removeFromLeft (136) : juce::Rectangle<int> {});
    if (inlineSimulation) toolbar.removeFromLeft (6);
    // Keep the controls usable in a narrow viewport without squeezing the speed
    // entry or Keep pitch option. Wider windows retain the single row.
    if (getWidth () < (inlineSimulation ? 650 : 480))
        toolbar = bounds.removeFromTop (28).reduced (3, 2);
    const auto compact { getWidth () < 700 };
    auditionRateLabel.setText (compact ? "Speed" : "Audition speed", juce::dontSendNotification);
    auditionRateLabel.setBounds (toolbar.removeFromLeft (compact ? 44 : 90));
    auditionRateSlider.setBounds (toolbar.removeFromLeft (juce::jlimit (110, 210, toolbar.getWidth () - 98)));
    toolbar.removeFromLeft (4);
    preservePitchButton.setBounds (toolbar.removeFromLeft (94));
    durationInfo.setBounds (bounds.removeFromBottom (18));
    scrollbar.setBounds (bounds.removeFromBottom (12));
    timeline.setBounds (bounds.removeFromTop (juce::jmin (kTimelineHeight, bounds.getHeight () / 3)));
    const auto viewStart { waveform.getVisibleStartSample () };
    const auto viewLength { waveform.getWidth () * waveform.getSamplesPerPixel () };
    waveform.setBounds (bounds);
    if (viewLength > 0.0) waveform.setVisibleRange (viewStart, viewLength);
    markerOverlay.setBounds (bounds);
    publishView ();
}

void WaveformDisplay::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
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
