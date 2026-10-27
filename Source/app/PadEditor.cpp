#include "PadEditor.h"

#include "SampleTools.h"
#include "Theme.h"

PadEditor::PadEditor (Project& p) : project (p)
{
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    info.setColour (juce::Label::textColourId, Theme::textDim);

    for (auto* c : std::initializer_list<juce::Component*> { &title, &info, &nameLabel, &name, &volumeLabel, &volume,
                                                             &panLabel, &pan, &tuneLabel, &tune, &modeLabel, &mode,
                                                             &mono, &reverse, &normalize, &load, &clear })
        addAndMakeVisible (c);

    // Sound names become 8.3 file names on the card, so keep them short.
    name.setInputRestrictions (16);
    name.onTextChange = [this] { pad().name = name.getText(); edited(); };

    for (auto* s : { &volume, &pan, &tune })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    }

    volume.setRange (0, 100, 1);
    volume.onValueChange = [this] { pad().volume = (int) volume.getValue(); edited(); };

    pan.setRange (0, 100, 1);
    pan.textFromValueFunction = [] (double v)
    {
        const int offset = juce::roundToInt (v) - 50;
        return offset == 0 ? juce::String ("C") : (offset < 0 ? "L" : "R") + juce::String (std::abs (offset));
    };
    pan.onValueChange = [this] { pad().pan = (int) pan.getValue(); edited(); };

    // The MPC tunes in tenths of a semitone, +-12 semitones.
    tune.setRange (-12.0, 12.0, 0.1);
    tune.setTextValueSuffix (" st");
    tune.setDoubleClickReturnValue (true, 0.0);
    tune.onValueChange = [this] { pad().tune = juce::roundToInt (tune.getValue() * 10.0); edited(); };

    mode.addItem ("One shot (plays to the end)", 1);
    mode.addItem ("Mono (a new hit cuts the last)", 2);
    mode.addItem ("Note off (stops when released)", 3);
    mode.onChange = [this] { pad().mode = (PadMode) (mode.getSelectedId() - 1); edited(); };

    mono.onClick = [this] { pad().mono = mono.getToggleState(); edited(); };
    reverse.onClick = [this] { pad().reverse = reverse.getToggleState(); edited(); };
    normalize.onClick = [this] { pad().normalize = normalize.getToggleState(); edited(); };

    load.onClick = [this] { if (onLoadFile) onLoadFile(); };
    clear.onClick = [this] { if (onClear) onClear(); };

    refresh();
}

void PadEditor::setPad (int index)
{
    padIndex = index;
    refresh();
}

void PadEditor::edited()
{
    project.changed();
}

void PadEditor::refresh()
{
    auto& p = pad();
    const bool has = ! p.isEmpty();

    title.setText ("Pad " + padLabel (padIndex), juce::dontSendNotification);

    if (has)
    {
        const double seconds = p.audio->getNumSamples() / kMpcSampleRate;
        info.setText (juce::String (seconds, 2) + " s, " + (p.channelsOnMpc() == 2 ? "stereo" : "mono") + ", "
                          + SampleTools::formatMegabytes (p.memoryBytes()) + " of MPC memory. MIDI note "
                          + juce::String (noteForPad (padIndex)) + ".",
                      juce::dontSendNotification);
    }
    else
    {
        info.setText ("Empty. Drop a sound on the pad, or load one. MIDI note " + juce::String (noteForPad (padIndex)) + ".",
                      juce::dontSendNotification);
    }

    name.setText (p.name, juce::dontSendNotification);
    volume.setValue (p.volume, juce::dontSendNotification);
    pan.setValue (p.pan, juce::dontSendNotification);
    tune.setValue (p.tune / 10.0, juce::dontSendNotification);
    mode.setSelectedId ((int) p.mode + 1, juce::dontSendNotification);
    mono.setToggleState (p.mono || (has && p.audio->getNumChannels() == 1), juce::dontSendNotification);
    mono.setEnabled (has && p.audio->getNumChannels() > 1);
    reverse.setToggleState (p.reverse, juce::dontSendNotification);
    normalize.setToggleState (p.normalize, juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &name, &volume, &pan, &tune, &mode, &reverse, &normalize, &clear })
        c->setEnabled (has);
}

void PadEditor::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
}

void PadEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    title.setBounds (r.removeFromTop (30));
    info.setBounds (r.removeFromTop (44));
    r.removeFromTop (8);

    auto row = [&r] (juce::Label& label, juce::Component& control)
    {
        auto line = r.removeFromTop (28);
        label.setBounds (line.removeFromLeft (64));
        control.setBounds (line);
        r.removeFromTop (8);
    };

    row (nameLabel, name);
    row (volumeLabel, volume);
    row (panLabel, pan);
    row (tuneLabel, tune);
    row (modeLabel, mode);

    for (auto* t : { &mono, &reverse, &normalize })
        t->setBounds (r.removeFromTop (26));

    r.removeFromTop (12);
    auto buttons = r.removeFromTop (30);
    load.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 4));
    buttons.removeFromLeft (8);
    clear.setBounds (buttons);
}
