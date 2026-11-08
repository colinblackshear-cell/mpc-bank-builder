#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "AudioEngine.h"
#include "BankModel.h"
#include "KitPage.h"
#include "SamplerPage.h"
#include "SoundLibrary.h"

// The window: a header with the two pages, Sampler (record, edit, effects, chop) and Kits
// (pads and export to the MPC).
class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer
{
public:
    MainComponent();

    void openKitFile (const juce::File& pgm);
    // Opens an audio file in the sampler.
    void openAudioFile (const juce::File&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void showPage (int index);
    void showAudioSettings();

    juce::AudioFormatManager formats;
    Project project;
    AudioEngine engine;
    SoundLibrary library;

    juce::Label appTitle { {}, "MPC Bank Builder" };
    juce::TextButton samplerTab { "Sampler" }, kitsTab { "Kits" };
    juce::TextButton audioButton { "Audio/MIDI..." };

    KitPage kits;
    SamplerPage sampler;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
