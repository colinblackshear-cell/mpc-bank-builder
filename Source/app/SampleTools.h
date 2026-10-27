#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "BankModel.h"
#include "mpc/BankWriter.h"

namespace SampleTools
{
    // File types the app accepts: anything JUCE decodes (WAV, AIFF, MP3, FLAC, ...) plus .SND.
    juce::String audioFileWildcard (juce::AudioFormatManager&);
    bool isAudioFile (const juce::File&, juce::AudioFormatManager&);

    // Loads an audio file or MPC .SND as 1 or 2 channels at 44.1 kHz.
    SharedAudio load (const juce::File&, juce::AudioFormatManager&, juce::String& error);

    // Expands dropped folders into the audio files inside them, sorted by name.
    juce::Array<juce::File> expandDroppedFiles (const juce::StringArray& paths, juce::AudioFormatManager&);

    SharedAudio resample (const juce::AudioBuffer<float>&, double sourceRate);

    // The pad as it will sound on the MPC: reverse, normalize and mono applied, 16 bits.
    // `audio` replaces the pad's own audio, e.g. with its tempo-fitted version.
    mpc::Sound renderPad (const Pad&, SharedAudio audio = nullptr);
    SharedAudio fromSound (const mpc::Sound&);

    mpc::Bank toMpcBank (const Bank&);

    // Reads a .PGM and the .SND files next to it into a bank. Sounds that are missing are
    // listed in `missing`.
    std::optional<Bank> importProgram (const juce::File& pgm, juce::StringArray& missing, juce::String& error);

    juce::String formatMegabytes (size_t bytes);
}
