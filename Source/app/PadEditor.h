#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BankModel.h"

// Settings for the selected pad: name, volume, pan, tune, how it plays, and how it's saved.
class PadEditor : public juce::Component
{
public:
    explicit PadEditor (Project&);

    void setPad (int pad);
    void refresh();

    std::function<void()> onClear;
    std::function<void()> onLoadFile;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Pad& pad() { return project.currentBank().pads[(size_t) padIndex]; }
    void edited();

    Project& project;
    int padIndex = 0;

    juce::Label title;
    juce::Label info;
    juce::Label nameLabel { {}, "Name" };
    juce::TextEditor name;
    juce::Label volumeLabel { {}, "Volume" }, panLabel { {}, "Pan" }, tuneLabel { {}, "Tune" }, modeLabel { {}, "Plays" };
    juce::Slider volume, pan, tune;
    juce::ComboBox mode;
    juce::ToggleButton mono { "Save as mono (half the memory)" };
    juce::ToggleButton reverse { "Reverse" };
    juce::ToggleButton normalize { "Normalize" };
    juce::TextButton load { "Load sound..." };
    juce::TextButton clear { "Clear pad" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadEditor)
};
