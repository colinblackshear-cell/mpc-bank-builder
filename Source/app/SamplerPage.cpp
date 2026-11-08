#include "SamplerPage.h"

#include "SampleTools.h"
#include "Theme.h"

namespace
{
    constexpr int kPreviewDelayMs = 150;   // wait for a knob to settle before re-rendering
    constexpr double kMaxRecordSeconds = 600;

    struct KnobSpec
    {
        const char* label;
        double min, max, defaultValue, mid;   // mid: the value at the knob's centre (sets the skew)
        const char* suffix;
        int decimals;
        std::function<double (const SoundFx::Settings&)> get;
        std::function<void (SoundFx::Settings&, double)> set;
    };

    const std::vector<KnobSpec>& knobSpecs()
    {
        static const std::vector<KnobSpec> specs {
            { "Pitch", -24, 24, 0, 0, " st", 1, [] (auto& s) { return s.pitch; }, [] (auto& s, double v) { s.pitch = (float) v; } },
            { "Speed", 0.25, 4, 1, 1, "x", 2, [] (auto& s) { return s.speed; }, [] (auto& s, double v) { s.speed = (float) v; } },
            { "Low cut", 20, 8000, 20, 400, " Hz", 0, [] (auto& s) { return s.lowCut; }, [] (auto& s, double v) { s.lowCut = (float) v; } },
            { "High cut", 200, 20000, 20000, 3000, " Hz", 0, [] (auto& s) { return s.highCut; }, [] (auto& s, double v) { s.highCut = (float) v; } },
            { "Resonance", 0, 1, 0, 0.5, "", 2, [] (auto& s) { return s.resonance; }, [] (auto& s, double v) { s.resonance = (float) v; } },
            { "Drive", 0, 1, 0, 0.3, "", 2, [] (auto& s) { return s.drive; }, [] (auto& s, double v) { s.drive = (float) v; } },
            { "Bits", 2, 16, 16, 9, "", 0, [] (auto& s) { return s.bits; }, [] (auto& s, double v) { s.bits = juce::roundToInt (v); } },
            { "Rate", 1000, 44100, 44100, 11025, " Hz", 0, [] (auto& s) { return s.sampleRate; }, [] (auto& s, double v) { s.sampleRate = (float) v; } },
            { "Delay", 0.02, 1.5, 0.25, 0.3, " s", 2, [] (auto& s) { return s.delayTime; }, [] (auto& s, double v) { s.delayTime = (float) v; } },
            { "Feedback", 0, 0.95, 0.4, 0.45, "", 2, [] (auto& s) { return s.delayFeedback; }, [] (auto& s, double v) { s.delayFeedback = (float) v; } },
            { "Delay mix", 0, 1, 0, 0.5, "", 2, [] (auto& s) { return s.delayMix; }, [] (auto& s, double v) { s.delayMix = (float) v; } },
            { "Room", 0, 1, 0.5, 0.5, "", 2, [] (auto& s) { return s.reverbSize; }, [] (auto& s, double v) { s.reverbSize = (float) v; } },
            { "Reverb mix", 0, 1, 0, 0.5, "", 2, [] (auto& s) { return s.reverbMix; }, [] (auto& s, double v) { s.reverbMix = (float) v; } },
        };
        return specs;
    }

    juce::String formatTime (double seconds)
    {
        const int s = (int) seconds;
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2) + "." + juce::String ((int) (seconds * 10) % 10);
    }
}

SamplerPage::SamplerPage (AudioEngine& e, SoundLibrary& lib, juce::AudioFormatManager& formats)
    : engine (e), library (lib)
{
    for (auto* c : std::initializer_list<juce::Component*> {
             &libraryList, &wave, &recordButton, &recordInfo, &nameLabel, &name, &saveButton, &sendButton, &chopButton,
             &playButton, &playSelectionButton, &trimButton, &cutButton, &silenceButton, &fadeInButton,
             &fadeOutButton, &normalizeButton, &reverseButton, &undoButton, &redoButton, &chopLabel, &hitsButton,
             &sensitivity, &grid, &clearMarkersButton, &sliceCount, &hearEffects, &applyButton, &resetButton, &status })
        addAndMakeVisible (c);

    libraryList.onOpen = [this, &formats] (const SoundLibrary::Entry& entry)
    {
        juce::String error;
        auto audio = SampleTools::load (entry.file, formats, error);
        if (audio == nullptr)
            setStatus (error, true);
        else
            openSound (audio, entry.name);
    };

    recordButton.setColour (juce::TextButton::buttonColourId, Theme::warning.darker (0.3f));
    recordButton.onClick = [this] { toggleRecording(); };
    recordInfo.setColour (juce::Label::textColourId, Theme::textDim);

    name.setInputRestrictions (40);
    name.setJustification (juce::Justification::centredLeft);
    name.setTextToShowWhenEmpty ("Name this sound", Theme::textDim);
    saveButton.onClick = [this] { saveToLibrary(); };
    sendButton.onClick = [this] { sendToPad(); };
    chopButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    chopButton.setColour (juce::TextButton::textColourOffId, Theme::background);
    chopButton.onClick = [this] { chopToPads(); };

    wave.onChange = [this] { updateButtons(); };

    // Play doubles as Stop while something is playing.
    playButton.onClick = [this]
    {
        if (engine.getPreviewPosition() >= 0)
            engine.stopPreview();
        else
            play (false);
    };
    playSelectionButton.onClick = [this] { play (true); };

    trimButton.onClick = [this] { edit ([] (auto& b, auto sel) { return SoundFx::range (b, sel.getStart(), sel.getEnd()); }); };
    cutButton.onClick = [this] { edit ([] (auto& b, auto sel) { return SoundFx::removeRange (b, sel.getStart(), sel.getEnd()); }); };
    silenceButton.onClick = [this]
    {
        edit ([] (auto& b, auto)
        {
            const auto r = SoundFx::audibleRange (b);
            return SoundFx::range (b, r.getStart(), r.getEnd());
        });
    };
    fadeInButton.onClick = [this]
    {
        edit ([] (auto& b, auto sel)
        {
            juce::AudioBuffer<float> out (b);
            const auto r = sel.isEmpty() ? juce::Range<int> (0, juce::jmin (b.getNumSamples(), (int) (0.05 * kMpcSampleRate))) : sel;
            SoundFx::fade (out, r.getStart(), r.getEnd(), true);
            out.clear (0, r.getStart());   // silence before a fade-in
            return out;
        });
    };
    fadeOutButton.onClick = [this]
    {
        edit ([] (auto& b, auto sel)
        {
            juce::AudioBuffer<float> out (b);
            const int n = b.getNumSamples();
            const auto r = sel.isEmpty() ? juce::Range<int> (juce::jmax (0, n - (int) (0.05 * kMpcSampleRate)), n) : sel;
            SoundFx::fade (out, r.getStart(), r.getEnd(), false);
            out.clear (r.getEnd(), n - r.getEnd());
            return out;
        });
    };
    normalizeButton.onClick = [this] { edit ([] (auto& b, auto) { juce::AudioBuffer<float> out (b); SoundFx::normalize (out); return out; }); };
    reverseButton.onClick = [this]
    {
        edit ([] (auto& b, auto sel)
        {
            juce::AudioBuffer<float> out (b);
            const auto r = sel.isEmpty() ? juce::Range<int> (0, b.getNumSamples()) : sel;
            out.reverse (r.getStart(), r.getLength());
            return out;
        });
    };
    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };

    chopLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    sensitivity.setSliderStyle (juce::Slider::LinearHorizontal);
    sensitivity.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    sensitivity.setRange (0, 1);
    sensitivity.setValue (0.5);
    sensitivity.setTooltip ("How easily a hit is found. Higher finds quieter hits.");
    hitsButton.onClick = [this]
    {
        if (sound == nullptr)
            return;
        pushUndo();
        wave.setMarkers (SoundFx::snapToZeroCrossings (*sound, SoundFx::findHits (*sound, (float) sensitivity.getValue(), 63)));
        updateButtons();
    };
    sensitivity.onDragEnd = [this] { if (! wave.getMarkers().empty()) hitsButton.onClick(); };

    grid.setTextWhenNothingSelected ("Even slices");
    for (int n : { 2, 4, 8, 16, 32, 64 })
        grid.addItem (juce::String (n) + " slices", n);
    grid.onChange = [this]
    {
        if (sound == nullptr || grid.getSelectedId() == 0)
            return;
        pushUndo();
        wave.setMarkers (SoundFx::snapToZeroCrossings (*sound, SoundFx::gridMarkers (sound->getNumSamples(), grid.getSelectedId())));
        grid.setSelectedId (0, juce::dontSendNotification);
        updateButtons();
    };
    clearMarkersButton.onClick = [this] { pushUndo(); wave.setMarkers ({}); updateButtons(); };
    sliceCount.setColour (juce::Label::textColourId, Theme::textDim);

    for (const auto& spec : knobSpecs())
    {
        auto knob = std::make_unique<Knob>();
        auto& s = knob->slider;
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
        s.setRange (spec.min, spec.max);
        if (spec.mid > spec.min && spec.mid < spec.max)
            s.setSkewFactorFromMidPoint (spec.mid);
        s.setNumDecimalPlacesToDisplay (spec.decimals);
        s.setTextValueSuffix (spec.suffix);
        s.setValue (spec.defaultValue, juce::dontSendNotification);
        s.setDoubleClickReturnValue (true, spec.defaultValue);
        s.onValueChange = [this, &spec, &s] { spec.set (fx, s.getValue()); effectsChanged(); };

        knob->label.setText (spec.label, juce::dontSendNotification);
        knob->label.setJustificationType (juce::Justification::centred);
        knob->label.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (s);
        addAndMakeVisible (knob->label);
        knobs.push_back (std::move (knob));
    }

    hearEffects.setToggleState (true, juce::dontSendNotification);
    applyButton.onClick = [this] { applyEffects(); };
    resetButton.onClick = [this] { resetEffects(); };
    status.setColour (juce::Label::textColourId, Theme::textDim);

    setStatus ("Press Record to sample from your mic, or pick a sound on the left. Drag across the waveform to select, double-click to add a chop.");
    updateButtons();
    startTimerHz (30);
}

SamplerPage::~SamplerPage()
{
    if (engine.isRecording())
        engine.stopRecording();
}

void SamplerPage::visibilityChanged()
{
    updateButtons();
}

void SamplerPage::openSound (SharedAudio audio, const juce::String& soundName)
{
    engine.stopPreview();
    if (sound != nullptr)
        pushUndo();
    sound = std::move (audio);
    wave.setAudio (sound);
    wave.setMarkers ({});
    wave.setSelection ({});
    name.setText (soundName, juce::dontSendNotification);
    effectsChanged();
    updateButtons();
    setStatus ("Opened " + soundName + ".");
}

//==============================================================================
void SamplerPage::toggleRecording()
{
    if (engine.isRecording())
    {
        auto take = engine.stopRecording();
        if (take.getNumSamples() < (int) (0.05 * kMpcSampleRate))
        {
            setStatus ("That recording was too short.", true);
        }
        else
        {
            openSound (std::make_shared<juce::AudioBuffer<float>> (std::move (take)),
                       "Rec " + juce::Time::getCurrentTime().formatted ("%H%M%S"));
            setStatus ("Recorded " + formatTime (sound->getNumSamples() / kMpcSampleRate)
                       + ". Trim it, add effects, then save it or send it to the pads.");
        }
        updateButtons();
        return;
    }

    juce::String error;
    if (! engine.setInputEnabled (true, error))
    {
        setStatus (error, true);
        return;
    }

    engine.stopPreview();
    engine.startRecording();
    updateButtons();
}

void SamplerPage::play (bool selectionOnly)
{
    auto audio = (hearEffects.getToggleState() && preview != nullptr) ? preview : sound;
    if (audio == nullptr)
        return;

    auto sel = wave.getSelection();
    if (selectionOnly && sel.getLength() > 0)
    {
        // a selection on the dry sound maps onto the effected one by the speed change
        const double ratio = (double) audio->getNumSamples() / sound->getNumSamples();
        if (audio != sound)
            sel = { (int) (sel.getStart() * ratio), (int) (sel.getEnd() * ratio) };
        engine.preview (audio, sel.getStart(), sel.getEnd());
    }
    else
    {
        engine.preview (audio, 0, audio->getNumSamples());
    }
}

void SamplerPage::pushUndo()
{
    undoStack.push_back ({ sound, wave.getMarkers() });
    if (undoStack.size() > 50)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void SamplerPage::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back ({ sound, wave.getMarkers() });
    auto state = undoStack.back();
    undoStack.pop_back();
    sound = state.sound;
    wave.setAudio (sound, true);
    wave.setMarkers (state.markers);
    effectsChanged();
    updateButtons();
}

void SamplerPage::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back ({ sound, wave.getMarkers() });
    auto state = redoStack.back();
    redoStack.pop_back();
    sound = state.sound;
    wave.setAudio (sound, true);
    wave.setMarkers (state.markers);
    effectsChanged();
    updateButtons();
}

void SamplerPage::edit (std::function<juce::AudioBuffer<float> (const juce::AudioBuffer<float>&, juce::Range<int>)> operation)
{
    if (sound == nullptr)
        return;

    engine.stopPreview();
    pushUndo();
    const auto sel = wave.getSelection();
    auto result = std::make_shared<juce::AudioBuffer<float>> (operation (*sound, sel));

    // Edits that change the length move or drop the chop markers; keep the ones that still fit.
    const bool sameLength = result->getNumSamples() == sound->getNumSamples();
    sound = result;
    wave.setAudio (sound, sameLength);
    if (! sameLength)
    {
        wave.setMarkers ({});
        wave.setSelection ({});
    }
    effectsChanged();
    updateButtons();
}

//==============================================================================
void SamplerPage::effectsChanged()
{
    if (sound == nullptr || fx.isNeutral())
    {
        preview = nullptr;
        previewDirty = false;
        updateButtons();
        return;
    }

    previewDirty = true;
    previewDueAt = juce::Time::getMillisecondCounter() + kPreviewDelayMs;
    updateButtons();
}

void SamplerPage::renderPreview()
{
    previewDirty = false;
    if (sound == nullptr || fx.isNeutral())
    {
        preview = nullptr;
        return;
    }

    juce::MouseCursor::showWaitCursor();
    preview = std::make_shared<juce::AudioBuffer<float>> (SoundFx::apply (*sound, fx));
    juce::MouseCursor::hideWaitCursor();
}

void SamplerPage::resetEffects()
{
    fx = {};
    const auto& specs = knobSpecs();
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->slider.setValue (specs[i].defaultValue, juce::dontSendNotification);
    effectsChanged();
}

void SamplerPage::applyEffects()
{
    if (sound == nullptr || fx.isNeutral())
        return;

    if (previewDirty || preview == nullptr)
        renderPreview();

    pushUndo();
    // markers follow the speed change
    const double ratio = 1.0 / fx.speed;
    std::vector<int> moved;
    for (int m : wave.getMarkers())
        moved.push_back ((int) (m * ratio));

    sound = preview;
    wave.setAudio (sound);
    wave.setMarkers (moved);
    resetEffects();
    setStatus ("Effects applied. Undo takes them off again.");
}

//==============================================================================
SharedAudio SamplerPage::output (bool selectionOnly)
{
    if (sound == nullptr)
        return nullptr;

    const auto sel = wave.getSelection();
    auto source = selectionOnly && sel.getLength() > 0
                    ? std::make_shared<juce::AudioBuffer<float>> (SoundFx::range (*sound, sel.getStart(), sel.getEnd()))
                    : sound;

    if (fx.isNeutral())
        return source;

    return std::make_shared<juce::AudioBuffer<float>> (SoundFx::apply (*source, fx));
}

std::vector<SamplerPage::Named> SamplerPage::slices()
{
    std::vector<Named> out;
    if (sound == nullptr)
        return out;

    const auto base = name.getText().trim().isNotEmpty() ? name.getText().trim() : juce::String ("Chop");
    int index = 1;
    for (auto& s : SoundFx::slice (*sound, wave.getMarkers()))
    {
        auto audio = fx.isNeutral() ? std::make_shared<juce::AudioBuffer<float>> (std::move (s))
                                    : std::make_shared<juce::AudioBuffer<float>> (SoundFx::apply (s, fx));
        out.push_back ({ base + " " + juce::String (index++), audio });
    }
    return out;
}

void SamplerPage::saveToLibrary()
{
    auto audio = output (true);
    if (audio == nullptr)
        return;

    const auto soundName = name.getText().trim().isNotEmpty() ? name.getText().trim() : juce::String ("Sound");
    const auto file = library.save (*audio, soundName);
    if (file == juce::File())
        setStatus ("Couldn't save to " + library.getFolder().getFullPathName(), true);
    else
        setStatus ("Saved " + file.getFileName() + (wave.hasSelection() ? " (the selection)" : "") + " to your sounds.");
}

void SamplerPage::sendToPad()
{
    auto audio = output (true);
    if (audio == nullptr || ! onSendToPads)
        return;

    const auto target = describeTarget ? describeTarget() : juce::String ("the kit");
    onSendToPads ({ { name.getText().trim().isNotEmpty() ? name.getText().trim() : juce::String ("Sound"), audio } });
    setStatus ("Sent to " + target + ".");
}

void SamplerPage::chopToPads()
{
    if (sound == nullptr || ! onSendToPads)
        return;

    if (wave.getMarkers().empty())
    {
        setStatus ("Add chops first: Find hits, pick even slices, or double-click the waveform.", true);
        return;
    }

    juce::MouseCursor::showWaitCursor();
    auto pieces = slices();
    juce::MouseCursor::hideWaitCursor();

    const auto target = describeTarget ? describeTarget() : juce::String ("the kit");
    onSendToPads (pieces);
    setStatus ("Chopped into " + juce::String ((int) pieces.size()) + " pads starting at " + target + ".");
}

//==============================================================================
void SamplerPage::timerCallback()
{
    meterLevel = juce::jmax (engine.takeInputLevel(), meterLevel * 0.85f);
    repaint (meterBounds);

    if (engine.isRecording())
    {
        engine.collectRecording();
        const auto seconds = engine.getRecordedSeconds();
        recordInfo.setText ("Recording " + formatTime (seconds), juce::dontSendNotification);
        if (seconds >= kMaxRecordSeconds)
            toggleRecording();
    }

    if (previewDirty && juce::Time::getMillisecondCounter() >= previewDueAt)
        renderPreview();

    playButton.setButtonText (engine.getPreviewPosition() >= 0 ? "Stop" : "Play");

    wave.setPlayhead ([this]
    {
        const int pos = engine.getPreviewPosition();
        if (pos < 0 || sound == nullptr)
            return -1;
        // the preview may be the effected sound; show its position on the dry one
        const bool effected = hearEffects.getToggleState() && preview != nullptr;
        return effected ? (int) (pos * (double) sound->getNumSamples() / preview->getNumSamples()) : pos;
    }());
}

void SamplerPage::updateButtons()
{
    const bool has = sound != nullptr;
    const bool rec = engine.isRecording();
    const bool sel = wave.hasSelection();

    recordButton.setButtonText (rec ? "Stop recording" : "Record");
    if (! rec)
        recordInfo.setText (engine.isInputEnabled() ? "Input on" : "Mic off until you record", juce::dontSendNotification);

    for (auto* b : { &playButton, &silenceButton, &fadeInButton, &fadeOutButton, &normalizeButton,
                     &reverseButton, &hitsButton, &saveButton, &sendButton })
        b->setEnabled (has && ! rec);
    grid.setEnabled (has && ! rec);
    for (auto* b : { &playSelectionButton, &trimButton, &cutButton })
        b->setEnabled (has && sel && ! rec);
    clearMarkersButton.setEnabled (has && ! wave.getMarkers().empty());
    chopButton.setEnabled (has && ! wave.getMarkers().empty() && ! rec);
    undoButton.setEnabled (! undoStack.empty());
    redoButton.setEnabled (! redoStack.empty());
    applyButton.setEnabled (has && ! fx.isNeutral());
    resetButton.setEnabled (! fx.isNeutral());

    const auto count = wave.getMarkers().size();
    sliceCount.setText (count == 0 ? juce::String() : juce::String ((int) count + 1) + " slices", juce::dontSendNotification);

    if (describeTarget)
    {
        const auto target = describeTarget();
        sendButton.setButtonText ((sel ? "Send selection to " : "Send to ") + target.upToFirstOccurrenceOf (" of ", false, false));
        chopButton.setButtonText ("Chop to pads from " + target.upToFirstOccurrenceOf (" of ", false, false).fromFirstOccurrenceOf ("pad ", false, false));
    }
    saveButton.setButtonText (sel ? "Save selection" : "Save to library");
}

void SamplerPage::setStatus (const juce::String& text, bool warning)
{
    status.setText (text, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, warning ? Theme::warning : Theme::textDim);
}

void SamplerPage::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // input meter
    g.setColour (Theme::padEmpty);
    g.fillRoundedRectangle (meterBounds.toFloat(), 3.0f);
    const float level = juce::jlimit (0.0f, 1.0f, std::sqrt (meterLevel));
    g.setColour (meterLevel > 0.98f ? Theme::warning : Theme::accent);
    g.fillRoundedRectangle (meterBounds.toFloat().withWidth (meterBounds.getWidth() * level), 3.0f);
}

void SamplerPage::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    status.setBounds (r.removeFromBottom (24));
    r.removeFromBottom (6);

    libraryList.setBounds (r.removeFromLeft (230));
    r.removeFromLeft (16);

    auto top = r.removeFromTop (34);
    recordButton.setBounds (top.removeFromLeft (140));
    top.removeFromLeft (12);
    meterBounds = top.removeFromLeft (110).withSizeKeepingCentre (110, 10);
    top.removeFromLeft (12);
    recordInfo.setBounds (top.removeFromLeft (200));

    // where the sound goes: name it, then save it, send it to a pad, or chop it onto pads
    auto out = r.removeFromBottom (34);
    r.removeFromBottom (12);
    chopButton.setBounds (out.removeFromRight (200));
    out.removeFromRight (8);
    sendButton.setBounds (out.removeFromRight (190));
    out.removeFromRight (8);
    saveButton.setBounds (out.removeFromRight (140));
    out.removeFromRight (16);
    nameLabel.setBounds (out.removeFromLeft (46));
    name.setBounds (out);

    r.removeFromTop (10);
    auto knobArea = r.removeFromBottom (112);
    r.removeFromBottom (10);
    auto chopRow = r.removeFromBottom (30);
    r.removeFromBottom (8);
    auto editRow = r.removeFromBottom (30);
    r.removeFromBottom (10);
    wave.setBounds (r);

    auto place = [] (juce::Rectangle<int>& row, juce::Component& c, int width)
    {
        c.setBounds (row.removeFromLeft (width));
        row.removeFromLeft (6);
    };

    place (editRow, playButton, 60);
    place (editRow, playSelectionButton, 108);
    editRow.removeFromLeft (10);
    place (editRow, trimButton, 112);
    place (editRow, cutButton, 60);
    place (editRow, silenceButton, 96);
    place (editRow, fadeInButton, 64);
    place (editRow, fadeOutButton, 72);
    place (editRow, normalizeButton, 84);
    place (editRow, reverseButton, 70);
    redoButton.setBounds (editRow.removeFromRight (56));
    editRow.removeFromRight (6);
    undoButton.setBounds (editRow.removeFromRight (56));

    place (chopRow, chopLabel, 46);
    place (chopRow, hitsButton, 90);
    place (chopRow, sensitivity, 140);
    place (chopRow, grid, 130);
    place (chopRow, clearMarkersButton, 60);
    place (chopRow, sliceCount, 90);

    auto fxButtons = knobArea.removeFromRight (130);
    hearEffects.setBounds (fxButtons.removeFromTop (30));
    fxButtons.removeFromTop (6);
    applyButton.setBounds (fxButtons.removeFromTop (32));
    fxButtons.removeFromTop (6);
    resetButton.setBounds (fxButtons.removeFromTop (32));

    const int knobWidth = knobArea.getWidth() / juce::jmax (1, (int) knobs.size());
    for (auto& k : knobs)
    {
        auto cell = knobArea.removeFromLeft (knobWidth);
        k->label.setBounds (cell.removeFromTop (16));
        k->slider.setBounds (cell);
    }
}
