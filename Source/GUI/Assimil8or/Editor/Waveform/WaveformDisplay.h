#pragma once

#include <JuceHeader.h>
#include "../SampleManager/SampleManagerProperties.h"
#include "../SampleManager/SampleProperties.h"
#include "../../../../Assimil8or/Preset/ChannelProperties.h"
#include "../../../../Assimil8or/Preset/ZoneProperties.h"
#include "../../../../Assimil8or/Audio/AudioPlayerProperties.h"
#include "RegionMove.h"
#include "RegionMoveWaveform.h"
#include "WaveformMarkers.h"
#include "WaveformRuler.h"

//==============================================================================
/**
    WaveformDisplay - the zone's sample, its start/end and its loop points.

    The waveform and basic gestures use oolib. App-local layers add grouped
    ruler values, collision-aware labels, region movement, menus and selection
    shading. Marker constraints and property bindings remain A8Manager's.

    Layout is a timeline strip over the waveform, with the marker overlay on top
    of the waveform sharing its bounds - the overlay maps sample <-> pixel
    through the waveform, and the timeline is handed the same view, so all three
    stay locked together while panning and zooming.
*/
class WaveformDisplay : public juce::Component, private juce::ScrollBar::Listener
{
public:
    WaveformDisplay ();
    void focusZone ();
    void setLoopSelected (bool loopSelected);
    void setExpanded (bool expanded);
    void refreshSimulationControls ();
    std::function<void (bool)> onRegionSelected;
    std::function<void ()> onExpandRequested;
    std::function<bool ()> canTriggerSimulation;
    std::function<void ()> onTriggerSimulation;

    void init (juce::ValueTree channelPropertiesVT, juce::ValueTree rootPropertiesVT);
    void setZone (int zoneIndex);

private:
    friend struct WaveformTestAccess;
    friend struct AudioAuditTestAccess;
    friend struct SimulationUiTestAccess;
    // Marker list indices, in the order they are added to the overlay.
    enum MarkerIndex
    {
        kSampleStart = 0,
        kSampleEnd,
        kLoopStart,
        kLoopEnd
    };

    static constexpr int kTimelineHeight { 20 };

    ChannelProperties channelProperties;
    SampleManagerProperties sampleManagerProperties;
    ZoneProperties zoneProperties;
    SampleProperties sampleProperties;
    AudioPlayerProperties audioPlayerProperties;
    double playheadSample { -1.0 };
    bool loopSelected { false }, expanded { false };
    unsigned int sourceGeneration { 0 };
    unsigned int matchGeneration { 0 };
    WaveformRuler timeline;
    RegionMoveWaveform waveform;
    RegionMarkerOverlay markerOverlay;
    std::optional<RegionMove::Region> movingRegion;
    // Draw the expand symbol ourselves: font fallback can replace a Unicode
    // arrow with an ellipsis in this compact button on some platforms.
    class ExpandButton : public juce::TextButton
    {
    public:
        ExpandButton () : juce::TextButton ("Expand waveform") {}
    private:
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            getLookAndFeel ().drawButtonBackground (g, *this, findColour (buttonColourId), over, down);
            g.setColour (findColour (textColourOffId).withMultipliedAlpha (isEnabled () ? 1.0f : 0.4f));
            const auto icon { getLocalBounds ().toFloat ().withSizeKeepingCentre (12.0f, 12.0f) };
            const auto left { icon.getX () }, right { icon.getRight () }, top { icon.getY () }, bottom { icon.getBottom () };
            g.drawLine (left, bottom, right, top, 1.5f);
            if (getToggleState ()) g.drawLine (left, top, right, bottom, 1.5f);
            else
            {
                g.drawLine (left, bottom, left, bottom - 5.0f, 1.5f);
                g.drawLine (left, bottom, left + 5.0f, bottom, 1.5f);
                g.drawLine (right, top, right - 5.0f, top, 1.5f);
                g.drawLine (right, top, right, top + 5.0f, 1.5f);
            }
        }
    } expandButton;
    juce::TextButton zoomIn { "+" }, zoomOut { "-" }, menuButton { juce::String::fromUTF8 ("\xe2\x9a\x99") };
    juce::TextButton zoomInfo;
    juce::TextButton simulationButton { "Sample > Loop" };
    juce::Label durationInfo;
    juce::Label auditionRateLabel;
    juce::Slider auditionRateSlider;
    juce::ToggleButton preservePitchButton { "Keep pitch" };
    juce::ScrollBar scrollbar { false };
    void scrollBarMoved (juce::ScrollBar*, double start) override;
    void focusRange (double start, double end);
    void focusLoop ();
    void resetZoom ();
    void jumpToMarker (int markerIndex);
    void showWaveformMenu (std::optional<double> clickedSample);
    void applyMenuAction (int action, std::optional<double> clickedSample);
    juce::PopupMenu buildWaveformMenu (std::optional<double> clickedSample);
    void selectRegion (bool loop);
    bool beginRegionMove (juce::Point<float> point);
    void setMarker (int marker, double position, bool keepOppositeBoundary = false);
    void nudgeMarker (int marker, bool right);
    void matchMarker (int marker, bool right);
    struct BoundaryMoveRequest;
    void beginBoundaryMove (int marker, bool right, bool matching);
    bool isCurrentMove (const BoundaryMoveRequest& request);
    void continueBoundaryMatch (std::shared_ptr<BoundaryMoveRequest> request);
    void finishBoundaryMove (std::shared_ptr<BoundaryMoveRequest> request, juce::int64 position);
    std::function<void (std::function<void ()>)> scheduleBoundaryMatch;
    std::function<void (const juce::String&, std::function<void (bool)>)> confirmBoundaryMatch;
    double markerPosition (int marker);
    juce::String markerLabel (int marker);
    void updateDurations ();
    bool isSimulatingThisZone ();
    void triggerSimulation ();

    bool hasSample ();
    juce::int64 getSampleLength ();
    int getDisplayChannel ();

    void setupColours ();
    void setupMarkers ();
    void updateAudioSource ();
    void updateDisplayChannel ();
    void updateMarkerPositions ();
    void publishView ();

    double constrainMarker (int markerIndex, double proposedPosition, bool keepOppositeBoundary = false);
    void markerMoved (int markerIndex);

    void enablementChanged () override;
    void resized () override;
    void paintOverChildren (juce::Graphics& g) override;
    void paint (juce::Graphics& g) override;
    bool keyPressed (const juce::KeyPress& key) override;
};
