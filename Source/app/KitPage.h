#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "AudioEngine.h"
#include "BankModel.h"
#include "PadEditor.h"
#include "PadGrid.h"
#include "SoundLibrary.h"

// Kits: the pad grid, pad settings, tempo, and export to the MPC's card.
class KitPage : public juce::Component,
                private juce::ChangeListener,
                private juce::ListBoxModel
{
public:
    KitPage (Project&, AudioEngine&, juce::AudioFormatManager&, SoundLibrary&);
    ~KitPage() override;

    // Opens an MPC program and the sounds next to it as a kit.
    void openKitFile (const juce::File& pgm);

    // Puts sounds on pads in order, starting at the selected pad.
    void placeSounds (const std::vector<std::pair<juce::String, SharedAudio>>&);
    // e.g. "pad A03 of Kit 1"
    juce::String describeSelectedPad() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int row) override;

    void selectPad (int pad);
    void loadFilesIntoPads (int firstPad, const juce::Array<juce::File>& files);
    void chooseFileForPad();
    void openKit();
    void exportToCard();
    void writeBanks (const juce::File& destination);
    void refresh();
    void setStatus (const juce::String& text, bool warning = false);

    Project& project;
    AudioEngine& engine;
    juce::AudioFormatManager& formats;

    juce::Label kitLabel { {}, "Kit name" };
    juce::TextEditor kitName;
    juce::Label memory;
    juce::ComboBox ram;
    juce::TextButton openButton { "Open kit..." };
    juce::Label bpmLabel { {}, "BPM" };
    juce::Slider bpm;
    juce::ToggleButton keepInTime { "Keep in time" };
    juce::TextButton exportButton { "Export to card..." };

    juce::ListBox kitList { "Kits", this };
    juce::TextButton newKit { "New kit" };
    juce::TextButton removeKit { "Remove" };
    LibraryList libraryList;

    std::array<juce::TextButton, 4> bankTabs;
    PadGrid grid { project };
    PadEditor editor { project };
    juce::Label status;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitPage)
};
