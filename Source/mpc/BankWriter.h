#pragma once

// Turns a bank of pads into a folder the MPC2000XL can load: one .PGM program plus the .SND
// files it plays, all with 8.3 names. No JUCE dependency.

#include "MpcFiles.h"

#include <filesystem>

namespace mpc
{

struct BankPad
{
    int pad = 0;                  // 0..63 (A01 = 0, B01 = 16, ...)
    Sound sound;                  // sound.name is a suggestion; the writer makes it card-safe
    uint8_t volume = 100;         // 0..100
    uint8_t pan = 50;             // 0..100
    int16_t tune = 0;             // tenths of a semitone
    VoiceOverlap voiceOverlap = VoiceOverlap::poly;
};

struct Bank
{
    std::string name;             // shown on the MPC as the program name (up to 16 characters)
    std::vector<BankPad> pads;
};

struct BankFiles
{
    std::string folderName;       // e.g. "BREAKS1"
    Program program;
    std::vector<Sound> sounds;    // names already unique and card-safe
};

// Builds the program and sounds without touching the disk.
BankFiles buildBank (const Bank& bank, std::vector<std::string>& takenFolderNames);

struct WriteResult
{
    bool ok = false;
    std::string error;
    std::filesystem::path folder;
    size_t memoryBytes = 0;
};

// Writes `files` into destination/<folderName>/, replacing any .SND and .PGM files already
// there, so a re-export never leaves stale sounds behind.
WriteResult writeBankFolder (const std::filesystem::path& destination, const BankFiles& files);

// Deletes the "._*" files macOS leaves next to copied files, in `folder` and its subfolders.
// The MPC lists them as broken files. Returns how many were removed.
int removeMacMetadataFiles (const std::filesystem::path& folder);

} // namespace mpc
