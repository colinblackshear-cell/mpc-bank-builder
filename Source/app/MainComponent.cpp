#include "MainComponent.h"

#include "SampleTools.h"
#include "Theme.h"

namespace
{
    juce::AudioFormatManager& registered (juce::AudioFormatManager& f)
    {
        f.registerBasicFormats();
        return f;
    }
}

MainComponent::MainComponent()
    : library (registered (formats)),
      kits (project, engine, formats, library),
      sampler (engine, library, formats)
{
    appTitle.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    appTitle.setColour (juce::Label::textColourId, Theme::accent);

    for (auto* tab : { &samplerTab, &kitsTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (2);
        tab->setColour (juce::TextButton::buttonOnColourId, Theme::accent);
        tab->setColour (juce::TextButton::textColourOnId, Theme::background);
    }
    samplerTab.onClick = [this] { showPage (0); };
    kitsTab.onClick = [this] { showPage (1); };
    audioButton.onClick = [this] { showAudioSettings(); };

    sampler.describeTarget = [this] { return kits.describeSelectedPad(); };
    sampler.onSendToPads = [this] (const std::vector<SamplerPage::Named>& sounds)
    {
        std::vector<std::pair<juce::String, SharedAudio>> list;
        for (const auto& s : sounds)
            list.emplace_back (s.name, s.audio);
        kits.placeSounds (list);
    };

    for (auto* c : std::initializer_list<juce::Component*> { &appTitle, &samplerTab, &kitsTab, &audioButton })
        addAndMakeVisible (c);
    addChildComponent (kits);
    addChildComponent (sampler);

    kitsTab.setToggleState (true, juce::dontSendNotification);
    showPage (1);
    setSize (1240, 800);
}

void MainComponent::openKitFile (const juce::File& pgm)
{
    kitsTab.setToggleState (true, juce::dontSendNotification);
    showPage (1);
    kits.openKitFile (pgm);
}

void MainComponent::openAudioFile (const juce::File& file)
{
    juce::String error;
    auto audio = SampleTools::load (file, formats, error);
    if (audio == nullptr)
        return;

    samplerTab.setToggleState (true, juce::dontSendNotification);
    showPage (0);
    sampler.openSound (audio, file.getFileNameWithoutExtension());
}

void MainComponent::showPage (int index)
{
    engine.stopPreview();
    sampler.setVisible (index == 0);
    kits.setVisible (index == 1);
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager(), 0, 2, 0, 2, true, false, true, false);
    selector->setSize (520, 460);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle = "Audio and MIDI settings";
    options.dialogBackgroundColour = Theme::panel;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
    g.setColour (Theme::panel);
    g.fillRect (0, 0, getWidth(), 56);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (56).reduced (16, 10);
    appTitle.setBounds (header.removeFromLeft (210));
    samplerTab.setBounds (header.removeFromLeft (100));
    header.removeFromLeft (6);
    kitsTab.setBounds (header.removeFromLeft (100));
    audioButton.setBounds (header.removeFromRight (120));

    kits.setBounds (r);
    sampler.setBounds (r);
}
