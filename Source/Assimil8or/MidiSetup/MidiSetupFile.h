#pragma once

#include <JuceHeader.h>
#include "MidiSetupProperties.h"

namespace MidiSetup
{
    static inline const juce::String ModeId { "mode" };
    static inline const juce::String AssignId { "assign" };
    static inline const juce::String BasicChannelId { "basicchannel" };
    static inline const juce::String RcvProgramChangeId { "rcvprogamchange" };
    static inline const juce::String XmtProgramChangeId { "xmtprogamchange" };
    static inline const juce::String ColACCId { "colAcc" };
    static inline const juce::String ColBCCId { "colBcc" };
    static inline const juce::String ColCCCId { "colCcc" };
    static inline const juce::String PitchWheelSemiId { "pitchwheelsemi" };
    static inline const juce::String VelocityDepthId { "velocitydepth" };
    static inline const juce::String NotificationsId { "notifications" };
    static inline const juce::String IndexBaseKeyId { "indexbasekey" };
};

class MidiSetupFile
{
public:
    MidiSetupFile () = default;
    juce::Result read (juce::File midiSetupFile);
    juce::Result write (juce::File presetFile, juce::ValueTree presetProperties);
    juce::ValueTree parse (juce::StringArray presetLines);
    juce::Result getParseResult () const { return parseResult; }
    juce::Result checkForExternalChanges () const;
    static bool settingsEqual (juce::ValueTree first, juce::ValueTree second);

    // Do not expose the immutable baseline used to detect and render edits.
    juce::ValueTree getMidiSetupPropertiesVT () { return midiSetupProperties.getValueTree ().createCopy (); }

private:
    MidiSetupProperties midiSetupProperties;
    juce::Result parseResult { juce::Result::ok () };
    juce::File sourceFile;
    juce::MemoryBlock sourceBytes;
    juce::String sourceText;
    bool sourceExisted { false };

    struct Line
    {
        juce::String text, ending;
        int parameter { -1 }, valueStart { 0 }, valueEnd { 0 };
    };
    std::vector<Line> sourceLines;

    juce::Result parseText (const juce::String& text);
    juce::String render (juce::ValueTree properties);
};
