#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "BankModel.h"

// Your own collection of sounds: a plain folder of WAV files, so you can also open it in Finder
// and add or remove sounds there. Recordings and edits from the sampler are saved here.
class SoundLibrary : public juce::ChangeBroadcaster
{
public:
    struct Entry
    {
        juce::File file;
        juce::String name;
        double seconds = 0;
        int channels = 0;
        juce::Time modified;
    };

    explicit SoundLibrary (juce::AudioFormatManager&, juce::File folder = defaultFolder());

    static juce::File defaultFolder();
    const juce::File& getFolder() const { return folder; }

    void rescan();
    const std::vector<Entry>& getEntries() const { return entries; }   // newest first

    // Saves `audio` (44.1 kHz) as a 24-bit WAV named after `name`, never overwriting a sound
    // already there. Returns the new file, or an empty File on failure.
    juce::File save (const juce::AudioBuffer<float>& audio, const juce::String& name);
    bool rename (const Entry&, const juce::String& newName);
    bool remove (const Entry&);   // moves the file to the Trash

    // Prefix for drag-and-drop descriptions of library sounds.
    static constexpr const char* dragPrefix = "library:";

private:
    juce::AudioFormatManager& formats;
    juce::File folder;
    std::vector<Entry> entries;
};

// The library as a searchable list. Click a sound to open it, or drag it onto a pad.
class LibraryList : public juce::Component,
                    private juce::ListBoxModel,
                    private juce::ChangeListener
{
public:
    explicit LibraryList (SoundLibrary&);
    ~LibraryList() override;

    std::function<void (const SoundLibrary::Entry&)> onOpen;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { filter(); }
    void filter();
    void showMenu (int row);

    SoundLibrary& library;
    std::vector<SoundLibrary::Entry> shown;
    juce::Label title { {}, "Sounds" };
    juce::TextEditor search;
    juce::ListBox list { "Sounds", this };
    juce::TextButton folderButton { "Show folder" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryList)
};
