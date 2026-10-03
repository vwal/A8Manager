#pragma once

#include <JuceHeader.h>
#include "oolib/ValueTree/ValueTreeWrapper.h"

class GuiProperties : public ValueTreeWrapper<GuiProperties>
{
public:
    GuiProperties () noexcept : ValueTreeWrapper<GuiProperties> (GuiTypeId)
    {
    }
    GuiProperties (juce::ValueTree vt, WrapperType wrapperType, EnableCallbacks shouldEnableCallbacks) noexcept
        : ValueTreeWrapper<GuiProperties> (GuiTypeId, vt, wrapperType, shouldEnableCallbacks)
    {
    }

    void setPosition (int x, int y, bool includeSelfCallback);
    void setSize (int width, int height, bool includeSelfCallback);
    void setPaneSizes (int pane1Size, int pane2Size, int pane3Size, bool includeSelfCallback);
    void setUiScale (double scale) { setValue (scale, UiScalePropertyId, false); }
    double getUiScale () { return std::clamp (getValue<double> (UiScalePropertyId), 1.0, 2.0); }
    void setLightAppearance (bool light) { setValue (light, LightAppearancePropertyId, false); }
    bool getLightAppearance () { return getValue<bool> (LightAppearancePropertyId); }
    void setAutoReduceAudition (bool enabled) { setValue (enabled, AutoReduceAuditionPropertyId, false); }
    bool getAutoReduceAudition () { return ! data.hasProperty (AutoReduceAuditionPropertyId) || getValue<bool> (AutoReduceAuditionPropertyId); }

    std::tuple<int,int> getPosition ();
    std::tuple<int, int> getSize ();
    std::tuple<int, int, int> getPaneSizes ();

    static inline const juce::Identifier GuiTypeId { "GUI" };
    static inline const juce::Identifier PositionPropertyId  { "position" };
    static inline const juce::Identifier SizePropertyId      { "size" };
    static inline const juce::Identifier PaneSizesPropertyId { "paneSizes" };
    static inline const juce::Identifier UiScalePropertyId { "uiScale" };
    static inline const juce::Identifier LightAppearancePropertyId { "lightAppearance" };
    static inline const juce::Identifier AutoReduceAuditionPropertyId { "autoReduceAudition" };

    void initValueTree ();
    void processValueTree ();

private:
};
