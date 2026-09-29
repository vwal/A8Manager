#pragma once
#include <JuceHeader.h>

// Keep global controls outside the scrolling editor, including on small screens.
struct WorkspaceHeaderLayout
{
    int height;
    juce::Rectangle<int> samples, designer, scaleLabel, scaleSelector, audioSettings, help,
                         outputDevice, appearanceLabel, appearanceSelector;

    static WorkspaceHeaderLayout forWidth (int width)
    {
        WorkspaceHeaderLayout layout;
        layout.height = 92;
        juce::Rectangle<int> row { 16, 10, juce::jmax (0, width - 32), 32 };
        layout.help = row.removeFromRight (104);
        row.removeFromRight (12);
        layout.audioSettings = row.removeFromRight (132);
        row.removeFromRight (12);
        layout.scaleSelector = row.removeFromRight (92);
        layout.scaleLabel = row.removeFromRight (78).withTrimmedRight (8);
        juce::Rectangle<int> workspaces { 16, 50, width - 32, 32 };
        layout.samples = workspaces.removeFromLeft (88);
        workspaces.removeFromLeft (6);
        layout.designer = workspaces.removeFromLeft (154);
        workspaces.removeFromLeft (12);
        layout.appearanceSelector = workspaces.removeFromRight (108);
        layout.appearanceLabel = workspaces.removeFromRight (88).withTrimmedRight (8);
        workspaces.removeFromRight (12);
        layout.outputDevice = workspaces;
        return layout;
    }
};
