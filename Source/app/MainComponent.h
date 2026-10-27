#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "AudioEngine.h"
#include "BankModel.h"
#include "PadEditor.h"
#include "PadGrid.h"

class MainComponent : public juce::Component,
                      private juce::ChangeListener,
                      private juce::ListBoxModel
{
public:
    MainComponent();
    ~MainComponent() override;

    // Opens an MPC program and the sounds next to it as a kit.
    void openKitFile (const juce::File& pgm);

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
    void showAudioSettings();
    void refresh();
    void setStatus (const juce::String& text, bool warning = false);

    Project project;
    AudioEngine engine;
    juce::AudioFormatManager formats;

    juce::Label appTitle { {}, "MPC Bank Builder" };
    juce::Label kitLabel { {}, "Kit name" };
    juce::TextEditor kitName;
    juce::Label memory;
    juce::ComboBox ram;
    juce::TextButton openButton { "Open kit..." };
    juce::TextButton audioButton { "Audio/MIDI..." };
    juce::TextButton exportButton { "Export to card..." };

    juce::ListBox kitList { "Kits", this };
    juce::TextButton newKit { "New kit" };
    juce::TextButton removeKit { "Remove" };

    std::array<juce::TextButton, 4> bankTabs;
    PadGrid grid { project };
    PadEditor editor { project };
    juce::Label status;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
