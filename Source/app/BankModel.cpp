#include "BankModel.h"

#include "SoundFx.h"

double Bank::beatsFor (int index) const
{
    const auto& pad = pads[(size_t) index];
    if (! keepInTime || pad.playNormal || pad.isEmpty())
        return 0;
    return pad.beats > 0 ? pad.beats : SoundFx::guessBeats (pad.audio->getNumSamples(), bpm);
}

SharedAudio Bank::audioToPlay (int index) const
{
    const auto& pad = pads[(size_t) index];
    const auto beats = beatsFor (index);

    if (beats <= 0)
        return pad.audio;

    if (pad.timed == nullptr || pad.timedSource != pad.audio.get() || std::abs (pad.timedBeats - beats) > 1.0e-9 || std::abs (pad.timedBpm - bpm) > 1.0e-9)
    {
        pad.timed = SoundFx::fitToTempo (pad.audio, beats, bpm);
        pad.timedSource = pad.audio.get();
        pad.timedBeats = beats;
        pad.timedBpm = bpm;
    }

    return pad.timed;
}

size_t Bank::memoryBytes (int index) const
{
    const auto& pad = pads[(size_t) index];
    if (pad.isEmpty())
        return 0;

    // A fitted pad's length follows from the tempo, so no need to stretch it just to measure.
    const auto beats = beatsFor (index);
    const auto frames = beats > 0 ? (size_t) std::round (beats * 60.0 / bpm * kMpcSampleRate)
                                  : (size_t) pad.audio->getNumSamples();
    return frames * (size_t) pad.channelsOnMpc() * 2;
}

size_t Bank::memoryBytes() const
{
    size_t total = 0;
    for (int i = 0; i < mpc::kPadCount; ++i)
        total += memoryBytes (i);
    return total;
}

int Bank::usedPads() const
{
    int n = 0;
    for (const auto& p : pads)
        n += p.isEmpty() ? 0 : 1;
    return n;
}

juce::String padLabel (int pad)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + pad / kPadsPerBank))
         + juce::String (pad % kPadsPerBank + 1).paddedLeft ('0', 2);
}

int noteForPad (int pad)
{
    static const auto map = mpc::Program::defaultPadToNote();
    return map[(size_t) juce::jlimit (0, mpc::kPadCount - 1, pad)];
}

int padForNote (int note)
{
    for (int pad = 0; pad < mpc::kPadCount; ++pad)
        if (noteForPad (pad) == note)
            return pad;
    return -1;
}

Project::Project()
{
    addBank();
}

void Project::setCurrent (int index)
{
    current = juce::jlimit (0, (int) banks.size() - 1, index);
    changed();
}

int Project::addBank (const juce::String& name)
{
    Bank bank;
    bank.name = name.isNotEmpty() ? name : "Kit " + juce::String ((int) banks.size() + 1);
    banks.push_back (std::move (bank));
    current = (int) banks.size() - 1;
    changed();
    return current;
}

void Project::removeBank (int index)
{
    if (index < 0 || index >= (int) banks.size())
        return;

    banks.erase (banks.begin() + index);

    if (banks.empty())
        addBank();

    current = juce::jlimit (0, (int) banks.size() - 1, current);
    changed();
}
