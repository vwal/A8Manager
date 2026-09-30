#pragma once
#include <JuceHeader.h>

// Read-only does not mean clipped: the filename can be selected/copied and
// scrolled horizontally. Choosing a replacement is a separate explicit action.
class SelectableFileLabel : public juce::Label
{
public:
    SelectableFileLabel ()
    {
        editor.setReadOnly (true);
        editor.setPopupMenuEnabled (false);
        editor.setMultiLine (false);
        editor.setScrollbarsShown (false);
        editor.setCaretVisible (true);
        editor.setSelectAllWhenFocused (false);
        editor.setIndents (3, 0);
        editor.setJustification (juce::Justification::centredLeft);
        editor.onContext = [this] { if (onPopupMenuCallback) onPopupMenuCallback (); };
        addAndMakeVisible (editor);
        choose.setButtonText ("...");
        choose.setTooltip ("Import samples from any folder into the current preset (or drop a WAV here)");
        choose.onClick = [this] { if (onChooseFilesRequested) onChooseFilesRequested (); else browse (); };
        addAndMakeVisible (choose);
        setTooltip ("Select and copy the filename; drag across its text to scroll. Use ... to choose another sample, or right-click for file actions.");
    }
    void setOutline (juce::Colour colour) { outline = colour; syncColours (); }
    void setFileFilter (juce::String filter) { fileFilter = std::move (filter); }
    void setDialogTitle (juce::String title) { dialogTitle = std::move (title); }
    void canMultiSelect (bool enabled) { multiSelect = enabled; }
    juce::TextEditor& textEditor () { return editor; }
    std::function<void (const juce::StringArray&)> onFilesSelected;
    std::function<void ()> onChooseFilesRequested;
    std::function<void ()> onPopupMenuCallback;
private:
    class FilenameEditor : public juce::TextEditor
    {
    public:
        std::function<void ()> onContext;
        bool keyPressed (const juce::KeyPress& key) override
        {
            // JUCE's read-only editor accepts Copy/Select All but rejects
            // navigation keys. Explicitly allow only caret/selection movement;
            // editing shortcuts still go through the read-only guard.
            const auto code { key.getKeyCode () };
            if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey ||
                code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey ||
                code == juce::KeyPress::upKey || code == juce::KeyPress::downKey ||
                code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey)
                return juce::TextEditorKeyMapper<FilenameEditor>::invokeKeyFunction (*this, key);
            return juce::TextEditor::keyPressed (key);
        }
        void mouseDown (const juce::MouseEvent& event) override
        {
            if (event.mods.isPopupMenu ()) { if (onContext) onContext (); }
            else juce::TextEditor::mouseDown (event);
        }
    } editor;
    juce::TextButton choose;
    juce::Colour outline;
    juce::String fileFilter, dialogTitle { "Choose a sample file" };
    bool multiSelect { true };
    std::unique_ptr<juce::FileChooser> chooser;
    void textWasChanged () override
    {
        editor.setText (getText (), false);
        editor.setCaretPosition (0);
        editor.setTooltip (getText () + "\nSelect/copy or drag to scroll; ... chooses another sample. Right-click for file actions.");
    }
    void colourChanged () override { syncColours (); }
    void lookAndFeelChanged () override { syncColours (); resized (); }
    void syncColours ()
    {
        editor.setColour (juce::TextEditor::textColourId, findColour (juce::Label::textColourId));
        editor.setColour (juce::TextEditor::backgroundColourId, findColour (juce::Label::backgroundColourId));
        editor.setColour (juce::TextEditor::outlineColourId, outline);
    }
    void resized () override
    {
        auto bounds { getLocalBounds () };
        choose.setBounds (bounds.removeFromRight (22));
        editor.setBounds (bounds);
        editor.setFont (getFont ());
    }
    void paint (juce::Graphics&) override {}
    void browse ()
    {
        chooser = std::make_unique<juce::FileChooser> (dialogTitle, juce::File {}, fileFilter);
        const auto options { juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
            (multiSelect ? juce::FileBrowserComponent::canSelectMultipleItems : 0) };
        chooser->launchAsync (options, [safe = juce::Component::SafePointer<SelectableFileLabel> (this)] (const juce::FileChooser& selected)
        {
            if (safe == nullptr) return;
            juce::StringArray files;
            for (const auto& url : selected.getURLResults ())
            {
                if (! url.isLocalFile () || url.getLocalFile ().isDirectory ()) return;
                files.add (url.getLocalFile ().getFullPathName ());
            }
            if (! files.isEmpty () && safe->onFilesSelected) safe->onFilesSelected (files);
        });
    }
};
