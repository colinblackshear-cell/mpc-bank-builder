#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "SoundFx.h"
#include "SoundLibrary.h"
#include "WaveEditor.h"

// Record from the microphone (or open a sound from the library), edit it, run it through
// effects, chop it, and send it to the kit's pads or save it to the library.
class SamplerPage : public juce::Component,
                    private juce::Timer
{
public:
    SamplerPage (AudioEngine&, SoundLibrary&, juce::AudioFormatManager&);
    ~SamplerPage() override;

    struct Named
    {
        juce::String name;
        SharedAudio audio;
    };

    // Sends sounds to the kit, starting at the kit's selected pad. Set by the main window.
    std::function<void (const std::vector<Named>&)> onSendToPads;
    // Describes where "send to pad" goes, e.g. "pad A03 of Kit 1".
    std::function<juce::String()> describeTarget;

    void openSound (SharedAudio, const juce::String& name);
    void visibilityChanged() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void toggleRecording();
    void play (bool selectionOnly);
    void edit (std::function<juce::AudioBuffer<float> (const juce::AudioBuffer<float>&, juce::Range<int>)>);
    void pushUndo();
    void undo();
    void redo();
    void effectsChanged();
    void renderPreview();
    void resetEffects();
    void applyEffects();
    void updateButtons();
    void setStatus (const juce::String&, bool warning = false);

    // What leaves the page: the sound (or selection) with effects applied, and chop slices.
    SharedAudio output (bool selectionOnly);
    std::vector<Named> slices();
    void saveToLibrary();
    void sendToPad();
    void chopToPads();

    AudioEngine& engine;
    SoundLibrary& library;

    SharedAudio sound;           // the sound being edited, before effects
    SharedAudio preview;         // the sound with effects, when any are set
    SoundFx::Settings fx;
    bool previewDirty = false;
    juce::uint32 previewDueAt = 0;

    struct State
    {
        SharedAudio sound;
        std::vector<int> markers;
    };
    std::vector<State> undoStack, redoStack;

    LibraryList libraryList { library };
    WaveEditor wave;

    juce::TextButton recordButton { "Record" };
    juce::Label recordInfo;
    float meterLevel = 0;
    juce::Rectangle<int> meterBounds;

    juce::Label nameLabel { {}, "Name" };
    juce::TextEditor name;
    juce::TextButton saveButton { "Save to library" };
    juce::TextButton sendButton { "Send to pad" };
    juce::TextButton chopButton { "Chop to pads" };

    juce::TextButton playButton { "Play" }, playSelectionButton { "Play selection" };
    juce::TextButton trimButton { "Keep selection" }, cutButton { "Delete" }, silenceButton { "Trim silence" };
    juce::TextButton fadeInButton { "Fade in" }, fadeOutButton { "Fade out" };
    juce::TextButton normalizeButton { "Normalize" }, reverseButton { "Reverse" };
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };

    juce::Label chopLabel { {}, "Chop" };
    juce::TextButton hitsButton { "Find hits" };
    juce::Slider sensitivity;
    juce::ComboBox grid;
    juce::TextButton clearMarkersButton { "Clear" };
    juce::Label sliceCount;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
    };
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::ToggleButton hearEffects { "Hear effects" };
    juce::TextButton applyButton { "Apply effects" }, resetButton { "Reset" };

    juce::Label status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerPage)
};
