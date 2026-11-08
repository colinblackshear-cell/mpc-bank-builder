#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "BankModel.h"

// Offline sound processing for the sampler: time-stretch and pitch, effects, chopping and
// tempo. Everything works on whole buffers at the MPC's 44.1 kHz and returns new ones.
namespace SoundFx
{
    // Changes length and pitch independently (pitch in semitones). Uses a phase vocoder, so
    // loops keep their pitch when they're sped up or slowed down.
    juce::AudioBuffer<float> stretch (const juce::AudioBuffer<float>&, int newLength, float semitones = 0.0f);

    struct Settings
    {
        float pitch = 0.0f;          // semitones, -24..24
        float speed = 1.0f;          // 0.25..4; 2 = half as long, same pitch
        float lowCut = 20.0f;        // Hz
        float highCut = 20000.0f;    // Hz
        float resonance = 0.0f;      // 0..1
        int bits = 16;               // 2..16, bit crusher
        float sampleRate = 44100.0f; // 1000..44100, sample rate reducer
        float drive = 0.0f;          // 0..1
        float delayTime = 0.25f;     // seconds
        float delayFeedback = 0.4f;  // 0..0.95
        float delayMix = 0.0f;       // 0..1
        float reverbSize = 0.5f;     // 0..1
        float reverbMix = 0.0f;      // 0..1

        bool isNeutral() const;
    };

    // Applies every effect in a fixed order: pitch/speed, filters, drive, crush, delay, reverb.
    // Delay and reverb tails make the result longer, up to 4 seconds; silence at the end is cut.
    juce::AudioBuffer<float> apply (const juce::AudioBuffer<float>&, const Settings&);

    // Edits
    juce::AudioBuffer<float> range (const juce::AudioBuffer<float>&, int start, int end);
    juce::AudioBuffer<float> removeRange (const juce::AudioBuffer<float>&, int start, int end);
    void fade (juce::AudioBuffer<float>&, int start, int end, bool fadeIn);
    void normalize (juce::AudioBuffer<float>&);
    // Range of the sound that is louder than threshold (as a gain, e.g. 0.01 = -40 dB).
    juce::Range<int> audibleRange (const juce::AudioBuffer<float>&, float threshold = 0.01f);

    // Chopping. Markers are sample positions where slices start; 0 is implied.
    std::vector<int> findHits (const juce::AudioBuffer<float>&, float sensitivity /* 0..1 */, int maxHits = 64);
    std::vector<int> gridMarkers (int length, int slices);
    // Moves each marker to the nearest zero crossing (within 5 ms) so slices don't click.
    std::vector<int> snapToZeroCrossings (const juce::AudioBuffer<float>&, std::vector<int> markers);
    std::vector<juce::AudioBuffer<float>> slice (const juce::AudioBuffer<float>&, const std::vector<int>& markers);

    // Tempo. A loop's length in beats is guessed as the power of two (1/4 to 64 beats) that
    // gives it a tempo closest to `bpm`. Sounds shorter than about a beat come back as 0,
    // which means "play normal": they're hits, not loops.
    double guessBeats (int lengthSamples, double bpm);
    double tempoOf (int lengthSamples, double beats);
    // The sound stretched so it lasts `beats` at `bpm`.
    SharedAudio fitToTempo (const SharedAudio&, double beats, double bpm);
}
