// Tests for the .SND/.PGM reader and writer. Plain C++, no JUCE.
//
// Set MPC_FIXTURES to a folder of files saved by an MPC2000XL (CI downloads the demo kits from
// the vMPC2000XL project) to also check that every one of them reads and writes back
// byte for byte.

#include "../Source/mpc/BankWriter.h"
#include "../Source/mpc/MpcFiles.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (! (cond)) {                                                                  \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n";   \
            ++failures;                                                                  \
        }                                                                                \
    } while (0)

static mpc::Bytes readFile (const fs::path& p)
{
    std::ifstream in (p, std::ios::binary);
    return mpc::Bytes (std::istreambuf_iterator<char> (in), {});
}

static mpc::Sound makeSound (const std::string& name, size_t frames, bool stereo)
{
    mpc::Sound s;
    s.name = name;
    for (size_t i = 0; i < frames; ++i)
    {
        s.left.push_back ((int16_t) (i * 7));
        if (stereo)
            s.right.push_back ((int16_t) -(int) (i * 3));
    }
    s.end = (uint32_t) frames;
    return s;
}

static void testNames()
{
    CHECK (mpc::toMpcName ("Kick drum.wav") == "KICKDRUM");
    CHECK (mpc::toMpcName ("snare #2") == "SNARE#2");
    CHECK (mpc::toMpcName ("hat*open") == "HAT_OPEN");
    CHECK (mpc::toMpcName ("") == "SOUND");
    CHECK (mpc::toMpcName ("a very long sound name", 16) == "AVERYLONGSOUNDNA");

    std::vector<std::string> taken;
    CHECK (mpc::makeUniqueName ("kick", taken) == "KICK");
    CHECK (mpc::makeUniqueName ("KICK", taken) == "KICK2");
    CHECK (mpc::makeUniqueName ("kick", taken) == "KICK3");
    CHECK (mpc::makeUniqueName ("breakbeat", taken) == "BREAKBEA");
    CHECK (mpc::makeUniqueName ("breakbeat", taken) == "BREAKB_2");
}

static void testSndRoundTrip()
{
    for (bool stereo : { false, true })
    {
        auto s = makeSound ("TEST", 1000, stereo);
        s.start = 10;
        s.loopTo = 500;
        s.loopEnabled = true;
        s.tune = -12;

        auto bytes = mpc::writeSnd (s);
        CHECK (bytes.size() == 42 + 1000 * 2 * (stereo ? 2 : 1));
        CHECK (bytes[0] == 0x01 && bytes[1] == 0x04);
        // name is padded with spaces and zero terminated
        CHECK (bytes[2] == 'T' && bytes[6] == ' ' && bytes[18] == 0);

        auto back = mpc::readSnd (bytes);
        CHECK (back.has_value());
        if (! back)
            continue;
        CHECK (back->name == "TEST");
        CHECK (back->left == s.left);
        CHECK (back->right == s.right);
        CHECK (back->start == 10 && back->end == 1000 && back->loopTo == 500);
        CHECK (back->loopEnabled && back->tune == -12 && back->sampleRate == 44100);
        CHECK (mpc::writeSnd (*back) == bytes);
    }

    CHECK (! mpc::readSnd (mpc::Bytes (10, 0)).has_value());
}

static void testNewProgram()
{
    mpc::Program p;
    p.name = "My Kit";
    auto bytes = mpc::writePgm (p);
    CHECK (bytes.size() == 2290);

    p.soundNames = { "KICK", "SNARE" };
    p.noteForPad (0).soundIndex = 0;
    bytes = mpc::writePgm (p);
    CHECK (bytes.size() == 2290 + 2 * 17);

    auto back = mpc::readPgm (bytes);
    CHECK (back.has_value());
    if (back)
    {
        CHECK (back->name == "My Kit");
        CHECK (back->soundNames.size() == 2 && back->soundNames[1] == "SNARE");
        CHECK (back->noteForPad (0).soundIndex == 0);
        CHECK (back->notes[37 - mpc::kFirstNote].soundIndex == 0);   // pad A01 plays note 37
        CHECK (back->noteForPad (1).soundIndex == mpc::kNoSound);
        CHECK (mpc::writePgm (*back) == bytes);
    }
}

static void testBankFolder()
{
    auto dir = fs::temp_directory_path() / "mpc-bank-builder-test";
    fs::remove_all (dir);
    fs::create_directories (dir / "BREAKS");

    // stale files from an earlier export, plus a macOS metadata file
    std::ofstream (dir / "BREAKS" / "OLD.SND") << "x";
    std::ofstream (dir / "BREAKS" / "._KICK.SND") << "x";
    std::ofstream (dir / "BREAKS" / "NOTES.TXT") << "keep me";

    mpc::Bank bank;
    bank.name = "Breaks";
    for (int i = 0; i < 3; ++i)
    {
        mpc::BankPad pad;
        pad.pad = i;
        pad.sound = makeSound ("break chop", 100, i == 1);
        pad.voiceOverlap = mpc::VoiceOverlap::noteOff;
        bank.pads.push_back (pad);
    }

    std::vector<std::string> folders;
    auto files = mpc::buildBank (bank, folders);
    CHECK (files.folderName == "BREAKS");
    CHECK (files.sounds.size() == 3);
    CHECK (files.sounds[0].name == "BREAKCHO");
    CHECK (files.sounds[1].name == "BREAKC_2");

    auto result = mpc::writeBankFolder (dir, files);
    CHECK (result.ok);
    CHECK (result.memoryBytes == 100 * 2 + 100 * 4 + 100 * 2);
    CHECK (fs::exists (dir / "BREAKS" / "BREAKS.PGM"));
    CHECK (fs::exists (dir / "BREAKS" / "BREAKCHO.SND"));
    CHECK (fs::exists (dir / "BREAKS" / "BREAKC_3.SND"));
    CHECK (! fs::exists (dir / "BREAKS" / "OLD.SND"));
    CHECK (! fs::exists (dir / "BREAKS" / "._KICK.SND"));
    CHECK (fs::exists (dir / "BREAKS" / "NOTES.TXT"));

    auto pgm = mpc::readPgm (readFile (dir / "BREAKS" / "BREAKS.PGM"));
    CHECK (pgm.has_value());
    if (pgm)
    {
        CHECK (pgm->name == "Breaks");
        CHECK (pgm->soundNames.size() == 3);
        for (int pad = 0; pad < 3; ++pad)
        {
            CHECK (pgm->noteForPad (pad).soundIndex == pad);
            CHECK (pgm->noteForPad (pad).voiceOverlap == mpc::VoiceOverlap::noteOff);
            // every sound the program names is a file next to it
            CHECK (fs::exists (dir / "BREAKS" / (pgm->soundNames[(size_t) pad] + ".SND")));
        }
    }

    auto snd = mpc::readSnd (readFile (dir / "BREAKS" / "BREAKC_2.SND"));
    CHECK (snd.has_value() && snd->isStereo() && snd->frameCount() == 100);

    // a second bank with the same name gets its own folder
    auto second = mpc::buildBank (bank, folders);
    CHECK (second.folderName == "BREAKS2");

    fs::remove_all (dir);
}

static void testHardwareFiles (const fs::path& fixtures)
{
    int snds = 0, pgms = 0;

    for (const auto& entry : fs::recursive_directory_iterator (fixtures))
    {
        if (! entry.is_regular_file())
            continue;

        auto ext = entry.path().extension().string();
        auto bytes = readFile (entry.path());
        std::string error;

        if (ext == ".SND" || ext == ".snd")
        {
            auto s = mpc::readSnd (bytes, &error);
            if (! s)
            {
                std::cerr << entry.path() << ": " << error << "\n";
                ++failures;
                continue;
            }
            if (mpc::writeSnd (*s) != bytes)
            {
                std::cerr << entry.path() << ": rewrite differs\n";
                ++failures;
            }
            ++snds;
        }
        else if (ext == ".PGM" || ext == ".pgm")
        {
            auto p = mpc::readPgm (bytes, &error);
            if (! p)
            {
                std::cerr << entry.path() << ": " << error << "\n";
                ++failures;
                continue;
            }
            if (mpc::writePgm (*p) != bytes)
            {
                std::cerr << entry.path() << ": rewrite differs\n";
                ++failures;
            }
            // our defaults for the effects board match what the MPC saves
            CHECK (p->effects == mpc::Program::defaultEffects());
            ++pgms;
        }
    }

    std::cout << "Checked " << snds << " sounds and " << pgms << " programs saved by an MPC\n";
    CHECK (snds > 0 && pgms > 0);
}

int main()
{
    testNames();
    testSndRoundTrip();
    testNewProgram();
    testBankFolder();

    if (const char* fixtures = std::getenv ("MPC_FIXTURES"))
        testHardwareFiles (fixtures);
    else
        std::cout << "MPC_FIXTURES not set, skipping the checks against real MPC files\n";

    if (failures == 0)
        std::cout << "All format tests passed\n";

    return failures == 0 ? 0 : 1;
}
