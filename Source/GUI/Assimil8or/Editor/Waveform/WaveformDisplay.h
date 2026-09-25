#pragma once

#include <JuceHeader.h>
#include "../EditManager.h"
#include "../SampleManager/SampleManagerProperties.h"
#include "../SampleManager/SampleProperties.h"
#include "../../../../Assimil8or/Preset/ChannelProperties.h"
#include "../../../../Assimil8or/Preset/ZoneProperties.h"
#include "../../../../Assimil8or/Audio/AudioPlayerProperties.h"
#include "RegionMove.h"
#include "RegionMoveWaveform.h"
#include "oolib/GUI/MarkerOverlay.h"
#include "oolib/GUI/TimelineComponent.h"

//==============================================================================
/**
    WaveformDisplay - the zone's sample, its start/end and its loop points.

    The drawing, the gestures and the marker editing all come from oolib
    (WaveformView / InteractiveWaveform / MarkerOverlay / TimelineComponent).
    What lives here is only the part that is A8Manager's: which ValueTree
    properties the four markers are bound to, and the rules about where they may
    go relative to each other (including the Loop Length as Loop End mode).

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

    void init (juce::ValueTree channelPropertiesVT, juce::ValueTree rootPropertiesVT);
    void setZone (int zoneIndex);

private:
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
    EditManager* editManager { nullptr };

    TimelineComponent timeline;
    RegionMoveWaveform waveform;
    RegionMarkerOverlay markerOverlay;
    juce::ComboBox editMode;
    std::optional<RegionMove::Region> movingRegion;
    juce::TextButton zoomIn { "+" }, zoomOut { "-" }, fit { "Fit" }, zone { "Zone" }, loop { "Loop" };
    juce::Label zoomInfo;
    juce::Label auditionRateLabel;
    juce::Slider auditionRateSlider;
    juce::ToggleButton preservePitchButton { "Keep pitch" };
    juce::ScrollBar scrollbar { false };
    void scrollBarMoved (juce::ScrollBar*, double start) override;
    void focusRange (double start, double end);

    bool hasSample ();
    juce::int64 getSampleLength ();
    int getDisplayChannel ();

    void setupColours ();
    void setupMarkers ();
    void updateAudioSource ();
    void updateDisplayChannel ();
    void updateMarkerPositions ();
    void updateEditMode ();
    void publishView ();

    double constrainMarker (int markerIndex, double proposedPosition);
    void markerMoved (int markerIndex);

    void enablementChanged () override;
    void resized () override;
    void paintOverChildren (juce::Graphics& g) override;
};
