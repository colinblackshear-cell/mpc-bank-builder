#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include "mpc/MpcFiles.h"

#include <array>
#include <memory>

// Everything the app works with: banks of 64 pads, each holding audio at the MPC's 44.1 kHz.

using SharedAudio = std::shared_ptr<const juce::AudioBuffer<float>>;

constexpr double kMpcSampleRate = 44100.0;
constexpr int kPadsPerBank = 16;

enum class PadMode
{
    oneShot,   // plays to the end every hit (MPC "poly")
    mono,      // a new hit cuts the last one off
    noteOff    // stops when you let go of the pad
};

struct Pad
{
    SharedAudio audio;              // 1 or 2 channels at 44.1 kHz; null when the pad is empty
    juce::String name;              // shown in the app and used for the .SND name
    int volume = 100;               // 0..100
    int pan = 50;                   // 0..100
    int tune = 0;                   // tenths of a semitone, -120..120
    PadMode mode = PadMode::oneShot;
    bool mono = false;              // save as mono to use half the MPC's memory
    bool reverse = false;
    bool normalize = false;

    // Tempo: with the kit's "keep in time" on, the pad is stretched to last `beats` at the
    // kit's BPM, unless `playNormal` is set. beats = 0 means "guess from the length".
    double beats = 0;
    bool playNormal = false;

    bool isEmpty() const { return audio == nullptr || audio->getNumSamples() == 0; }
    int channelsOnMpc() const { return isEmpty() ? 0 : (mono ? 1 : juce::jmin (2, audio->getNumChannels())); }

private:
    friend struct Bank;
    // The last tempo-fitted version of `audio`, kept so playing a pad doesn't stretch it again.
    mutable SharedAudio timed;
    mutable const void* timedSource = nullptr;
    mutable double timedBeats = 0, timedBpm = 0;
};

struct Bank
{
    juce::String name;
    std::array<Pad, mpc::kPadCount> pads;

    double bpm = 90.0;
    bool keepInTime = false;        // stretch every pad to `bpm`, except pads set to play normal

    // How many beats the pad will be stretched to, or 0 when it plays as recorded.
    double beatsFor (int pad) const;
    // The pad's audio as it plays and exports: tempo-fitted when keep in time applies.
    SharedAudio audioToPlay (int pad) const;

    size_t memoryBytes (int pad) const;
    size_t memoryBytes() const;
    int usedPads() const;
};

// Pad index 0..63 as the MPC labels it: "A01".."D16".
juce::String padLabel (int pad);

// The MIDI note the MPC plays each pad with, and back (-1 if no pad uses the note).
int noteForPad (int pad);
int padForNote (int note);

class Project : public juce::ChangeBroadcaster
{
public:
    Project();

    std::vector<Bank>& getBanks() { return banks; }
    const std::vector<Bank>& getBanks() const { return banks; }
    Bank& currentBank() { return banks[(size_t) current]; }
    int getCurrentIndex() const { return current; }
    void setCurrent (int index);

    int addBank (const juce::String& name = {});
    void removeBank (int index);

    // Call after changing anything, so the views refresh.
    void changed() { sendChangeMessage(); }

    int ramMegabytes = 32;          // the MPC's memory; 2 MB stock, up to 32 MB

private:
    std::vector<Bank> banks;
    int current = 0;
};
