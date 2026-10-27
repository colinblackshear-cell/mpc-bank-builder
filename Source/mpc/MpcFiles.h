#pragma once

// Reading and writing the MPC2000XL's native file formats (.SND sounds and .PGM programs).
// Plain C++17 with no JUCE dependency, so it can be unit tested on its own.
//
// Layouts were worked out from files saved by the MPC2000XL. Multi-byte values are little endian.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mpc
{

using Bytes = std::vector<uint8_t>;

//==============================================================================
// Names

// Sound and program names inside files are up to 16 characters.
constexpr int kNameLength = 16;
// File names on the card are DOS 8.3, so names we also use as file names stay within 8.
constexpr int kFileNameLength = 8;

// Uppercases and replaces anything the MPC can't show with '_'. Spaces and dots are dropped.
// Returns at most maxLength characters, never an empty string.
std::string toMpcName (const std::string& name, int maxLength = kFileNameLength);

// Makes `name` unique against `taken` (compared case-insensitively) by replacing its tail with
// a number, e.g. "KICK" -> "KICK2", "BREAKBEA" -> "BREAKB_2". Adds the result to `taken`.
std::string makeUniqueName (const std::string& name, std::vector<std::string>& taken,
                            int maxLength = kFileNameLength);

//==============================================================================
// .SND: 42-byte header followed by 16-bit samples. Stereo sounds store all of the left
// channel, then all of the right (not interleaved).

struct Sound
{
    std::string name;            // up to 16 characters
    std::vector<int16_t> left;
    std::vector<int16_t> right;  // empty for mono

    uint8_t level = 100;
    int8_t tune = 0;             // -120..120, tenths of a semitone
    uint32_t start = 0;          // playback start frame
    uint32_t end = 0;            // playback end frame (usually frameCount)
    uint32_t loopTo = 0;         // loop start frame
    bool loopEnabled = false;
    uint8_t beatCount = 4;
    uint16_t sampleRate = 44100;

    uint32_t frameCount() const { return (uint32_t) left.size(); }
    bool isStereo() const { return ! right.empty(); }
    // Bytes the sound takes in the MPC's sample memory.
    size_t memoryBytes() const { return (left.size() + right.size()) * sizeof (int16_t); }
};

Bytes writeSnd (const Sound& sound);
std::optional<Sound> readSnd (const Bytes& data, std::string* error = nullptr);

//==============================================================================
// .PGM: a list of sound names, then settings for the 64 notes (35..98) the pads play,
// a mixer strip per pad, the pad-to-note map and the effects settings.

constexpr int kPadCount = 64;       // 16 pads x banks A-D
constexpr int kFirstNote = 35;      // notes 35..98
constexpr uint8_t kNoSound = 0xFF;
constexpr uint8_t kNoteOff = 0;     // "OFF" for mute and also-play note fields

enum class VoiceOverlap : uint8_t
{
    poly = 0,     // every hit plays to the end (one shot)
    mono = 1,     // a new hit cuts off the previous one
    noteOff = 2   // the sound stops when you let go of the pad
};

struct NoteParameters
{
    uint8_t soundIndex = kNoSound;   // index into Program::soundNames
    uint8_t soundGenerationMode = 0; // 0 normal, 1 simult, 2 vel switch, 3 decay switch
    uint8_t velocityRangeLower = 44;
    uint8_t alsoPlayNote1 = kNoteOff;
    uint8_t velocityRangeUpper = 88;
    uint8_t alsoPlayNote2 = kNoteOff;
    VoiceOverlap voiceOverlap = VoiceOverlap::poly;
    uint8_t muteNote1 = kNoteOff;
    uint8_t muteNote2 = kNoteOff;
    int16_t tune = 0;                // -120..120, tenths of a semitone
    uint8_t attack = 0;
    uint8_t decay = 5;
    uint8_t decayMode = 0;           // 0 = decay from the end, 1 = from the start
    uint8_t cutoff = 100;
    uint8_t resonance = 0;
    uint8_t filterEnvAttack = 0;
    uint8_t filterEnvDecay = 0;
    uint8_t filterEnvAmount = 0;
    uint8_t velocityToLevel = 100;
    uint8_t velocityToAttack = 0;
    uint8_t velocityToStart = 0;
    uint8_t velocityToCutoff = 0;
    uint8_t sliderParameter = 0;
    uint8_t velocityToPitch = 0;
};

struct PadMixer
{
    uint8_t fxOutput = 0;
    uint8_t volume = 100;            // 0..100
    uint8_t pan = 50;                // 0 = left, 50 = centre, 100 = right
    uint8_t volumeIndividual = 100;
    uint8_t output = 0;
    uint8_t fxSendLevel = 0;
};

struct Program
{
    std::string name;                        // up to 16 characters
    std::vector<std::string> soundNames;     // each the name of a .SND file in the same folder
    std::array<NoteParameters, kPadCount> notes {};   // notes[i] is note kFirstNote + i
    std::array<PadMixer, kPadCount> mixers {};
    std::array<uint8_t, kPadCount> padToNote = defaultPadToNote();
    uint8_t programChange = 0;

    // Fields we don't edit, kept as read so a loaded program saves back unchanged.
    std::array<uint8_t, 2> header {{ 30, 0 }};
    std::array<uint8_t, 9> slider {{ 0, 136, 120, 12, 45, 0, 20, 206, 50 }};
    std::array<uint8_t, 5> afterProgramChange {{ 35, 64, 0, 25, 0 }};
    uint8_t afterNotes = 6;
    std::array<uint8_t, 3> afterMixers {{ 0, 64, 0 }};
    std::array<uint8_t, 200> effects = defaultEffects();

    NoteParameters& noteForPad (int pad) { return notes[(size_t) (padToNote[(size_t) pad] - kFirstNote)]; }
    const NoteParameters& noteForPad (int pad) const { return notes[(size_t) (padToNote[(size_t) pad] - kFirstNote)]; }

    // The MPC2000XL's factory pad layout (A01 plays note 37, A02 note 36, ...).
    static std::array<uint8_t, kPadCount> defaultPadToNote();
    // The effects board settings of a freshly initialised MPC2000XL.
    static std::array<uint8_t, 200> defaultEffects();
};

Bytes writePgm (const Program& program);
std::optional<Program> readPgm (const Bytes& data, std::string* error = nullptr);

} // namespace mpc
