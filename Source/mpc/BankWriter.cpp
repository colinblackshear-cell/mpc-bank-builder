#include "BankWriter.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace fs = std::filesystem;

namespace mpc
{

namespace
{
    std::string upperExtension (const fs::path& p)
    {
        auto ext = p.extension().string();
        std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return (char) std::toupper (c); });
        return ext;
    }

    bool writeFile (const fs::path& path, const Bytes& bytes)
    {
        std::ofstream out (path, std::ios::binary | std::ios::trunc);
        out.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
        return out.good();
    }
}

BankFiles buildBank (const Bank& bank, std::vector<std::string>& takenFolderNames)
{
    BankFiles files;
    files.folderName = makeUniqueName (bank.name, takenFolderNames);

    auto& program = files.program;
    program.name = bank.name.substr (0, kNameLength);

    // The program and its sounds share a folder, and a sound named like the program's file
    // would collide with nothing (different extension), so only sound names need to be unique.
    std::vector<std::string> taken;

    for (const auto& pad : bank.pads)
    {
        if (pad.pad < 0 || pad.pad >= kPadCount || pad.sound.frameCount() == 0)
            continue;

        if (files.sounds.size() >= 255)
            break;   // a program can reference at most 255 sounds (0xFF means none)

        auto sound = pad.sound;
        // The MPC finds a program's sounds by file name, so the name inside the .SND must be
        // the file name: 8 characters.
        sound.name = makeUniqueName (sound.name, taken);

        auto& note = program.noteForPad (pad.pad);
        note.soundIndex = (uint8_t) files.sounds.size();
        note.tune = pad.tune;
        note.voiceOverlap = pad.voiceOverlap;

        auto& mixer = program.mixers[(size_t) (program.padToNote[(size_t) pad.pad] - kFirstNote)];
        mixer.volume = std::min<uint8_t> (pad.volume, 100);
        mixer.pan = std::min<uint8_t> (pad.pan, 100);

        program.soundNames.push_back (sound.name);
        files.sounds.push_back (std::move (sound));
    }

    return files;
}

WriteResult writeBankFolder (const fs::path& destination, const BankFiles& files)
{
    WriteResult result;
    result.folder = destination / files.folderName;

    std::error_code ec;
    fs::create_directories (result.folder, ec);

    if (ec)
    {
        result.error = "Couldn't create " + result.folder.string() + ": " + ec.message();
        return result;
    }

    for (const auto& entry : fs::directory_iterator (result.folder, ec))
    {
        const auto ext = upperExtension (entry.path());

        if (entry.is_regular_file() && (ext == ".SND" || ext == ".PGM"))
            fs::remove (entry.path(), ec);
    }

    removeMacMetadataFiles (result.folder);

    for (const auto& sound : files.sounds)
    {
        if (! writeFile (result.folder / (sound.name + ".SND"), writeSnd (sound)))
        {
            result.error = "Couldn't write " + sound.name + ".SND. Is the card full or locked?";
            return result;
        }

        result.memoryBytes += sound.memoryBytes();
    }

    if (! writeFile (result.folder / (files.folderName + ".PGM"), writePgm (files.program)))
    {
        result.error = "Couldn't write " + files.folderName + ".PGM. Is the card full or locked?";
        return result;
    }

    result.ok = true;
    return result;
}

int removeMacMetadataFiles (const fs::path& folder)
{
    int removed = 0;
    std::error_code ec;

    for (auto it = fs::recursive_directory_iterator (folder, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment (ec))
    {
        if (ec)
            break;

        const auto name = it->path().filename().string();

        if (name.rfind ("._", 0) == 0 && it->is_regular_file (ec))
        {
            std::error_code removeError;
            if (fs::remove (it->path(), removeError))
                ++removed;
        }
    }

    return removed;
}

} // namespace mpc
