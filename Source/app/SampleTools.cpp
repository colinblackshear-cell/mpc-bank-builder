#include "SampleTools.h"

namespace
{
    mpc::Bytes readBytes (const juce::File& f)
    {
        juce::MemoryBlock block;
        f.loadFileAsData (block);
        auto* data = static_cast<const uint8_t*> (block.getData());
        return mpc::Bytes (data, data + block.getSize());
    }

    int16_t toInt16 (float v)
    {
        return (int16_t) juce::roundToInt (juce::jlimit (-1.0f, 1.0f, v) * 32767.0f);
    }

    mpc::VoiceOverlap toOverlap (PadMode m)
    {
        switch (m)
        {
            case PadMode::mono:    return mpc::VoiceOverlap::mono;
            case PadMode::noteOff: return mpc::VoiceOverlap::noteOff;
            case PadMode::oneShot: break;
        }
        return mpc::VoiceOverlap::poly;
    }

    PadMode fromOverlap (mpc::VoiceOverlap v)
    {
        switch (v)
        {
            case mpc::VoiceOverlap::mono:    return PadMode::mono;
            case mpc::VoiceOverlap::noteOff: return PadMode::noteOff;
            case mpc::VoiceOverlap::poly:    break;
        }
        return PadMode::oneShot;
    }
}

namespace SampleTools
{

juce::String audioFileWildcard (juce::AudioFormatManager& formats)
{
    return formats.getWildcardForAllFormats() + ";*.snd;*.SND";
}

bool isAudioFile (const juce::File& f, juce::AudioFormatManager& formats)
{
    return f.hasFileExtension ("snd") || formats.findFormatForFileExtension (f.getFileExtension()) != nullptr;
}

SharedAudio resample (const juce::AudioBuffer<float>& source, double sourceRate)
{
    if (std::abs (sourceRate - kMpcSampleRate) < 0.5 || sourceRate <= 0)
        return std::make_shared<juce::AudioBuffer<float>> (source);

    const double ratio = sourceRate / kMpcSampleRate;
    const int outLength = (int) std::ceil (source.getNumSamples() / ratio);
    // the interpolator looks a few samples ahead, so give it silence past the end
    const int padding = 64;

    auto out = std::make_shared<juce::AudioBuffer<float>> (source.getNumChannels(), outLength);
    std::vector<float> in ((size_t) (source.getNumSamples() + padding), 0.0f);

    for (int ch = 0; ch < source.getNumChannels(); ++ch)
    {
        std::copy (source.getReadPointer (ch), source.getReadPointer (ch) + source.getNumSamples(), in.begin());
        juce::WindowedSincInterpolator interpolator;
        interpolator.process (ratio, in.data(), out->getWritePointer (ch), outLength);
    }

    return out;
}

SharedAudio fromSound (const mpc::Sound& s)
{
    juce::AudioBuffer<float> buffer (s.isStereo() ? 2 : 1, (int) s.frameCount());

    for (int i = 0; i < (int) s.frameCount(); ++i)
    {
        buffer.setSample (0, i, s.left[(size_t) i] / 32768.0f);
        if (s.isStereo())
            buffer.setSample (1, i, s.right[(size_t) i] / 32768.0f);
    }

    return resample (buffer, s.sampleRate);
}

SharedAudio load (const juce::File& file, juce::AudioFormatManager& formats, juce::String& error)
{
    if (file.hasFileExtension ("snd"))
    {
        std::string why;
        auto sound = mpc::readSnd (readBytes (file), &why);

        if (! sound)
        {
            error = file.getFileName() + ": " + juce::String (why);
            return nullptr;
        }

        // honour the sound's trim points
        auto start = std::min (sound->start, sound->frameCount());
        auto end = std::min (std::max (sound->end, start), sound->frameCount());
        if (start > 0 || end < sound->frameCount())
        {
            sound->left = std::vector<int16_t> (sound->left.begin() + start, sound->left.begin() + end);
            if (sound->isStereo())
                sound->right = std::vector<int16_t> (sound->right.begin() + start, sound->right.begin() + end);
        }

        return fromSound (*sound);
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        error = file.getFileName() + " isn't an audio file the app can read";
        return nullptr;
    }

    const auto length = reader->lengthInSamples;
    // The MPC holds at most 32 MB, about 6 minutes of mono audio; refuse anything far longer.
    if (length <= 0 || length > (juce::int64) (kMpcSampleRate * 60 * 12))
    {
        error = file.getFileName() + (length <= 0 ? " is empty" : " is too long for the MPC");
        return nullptr;
    }

    const int sourceChannels = (int) reader->numChannels;
    juce::AudioBuffer<float> raw (sourceChannels, (int) length);
    reader->read (&raw, 0, (int) length, 0, true, true);

    // More than two channels: fold down to stereo.
    juce::AudioBuffer<float> stereo (juce::jmin (2, sourceChannels), (int) length);
    stereo.clear();
    for (int ch = 0; ch < sourceChannels; ++ch)
        stereo.addFrom (ch % stereo.getNumChannels(), 0, raw, ch, 0, (int) length,
                        sourceChannels > 2 ? 2.0f / (float) sourceChannels : 1.0f);

    return resample (stereo, reader->sampleRate);
}

juce::Array<juce::File> expandDroppedFiles (const juce::StringArray& paths, juce::AudioFormatManager& formats)
{
    juce::Array<juce::File> result;

    for (const auto& path : paths)
    {
        juce::File f (path);

        if (f.isDirectory())
        {
            auto inside = f.findChildFiles (juce::File::findFiles, false);
            inside.sort();
            for (const auto& child : inside)
                if (isAudioFile (child, formats))
                    result.add (child);
        }
        else if (isAudioFile (f, formats))
        {
            result.add (f);
        }
    }

    return result;
}

mpc::Sound renderPad (const Pad& pad)
{
    mpc::Sound sound;
    sound.name = pad.name.toStdString();

    if (pad.isEmpty())
        return sound;

    const auto& audio = *pad.audio;
    const int n = audio.getNumSamples();
    const int channels = pad.channelsOnMpc();

    juce::AudioBuffer<float> work (channels, n);

    if (channels == 1 && audio.getNumChannels() > 1)
    {
        work.copyFrom (0, 0, audio.getReadPointer (0), n, 0.5f);
        work.addFrom (0, 0, audio, 1, 0, n, 0.5f);
    }
    else
    {
        for (int ch = 0; ch < channels; ++ch)
            work.copyFrom (ch, 0, audio, ch, 0, n);
    }

    if (pad.reverse)
        work.reverse (0, n);

    if (pad.normalize)
    {
        const auto peak = work.getMagnitude (0, n);
        if (peak > 0.0001f)
            work.applyGain (0.98f / peak);
    }

    sound.left.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
        sound.left[(size_t) i] = toInt16 (work.getSample (0, i));

    if (channels == 2)
    {
        sound.right.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            sound.right[(size_t) i] = toInt16 (work.getSample (1, i));
    }

    sound.end = (uint32_t) n;
    return sound;
}

mpc::Bank toMpcBank (const Bank& bank)
{
    mpc::Bank out;
    out.name = bank.name.toStdString();

    for (int i = 0; i < mpc::kPadCount; ++i)
    {
        const auto& pad = bank.pads[(size_t) i];
        if (pad.isEmpty())
            continue;

        mpc::BankPad p;
        p.pad = i;
        p.sound = renderPad (pad);
        p.volume = (uint8_t) juce::jlimit (0, 100, pad.volume);
        p.pan = (uint8_t) juce::jlimit (0, 100, pad.pan);
        p.tune = (int16_t) juce::jlimit (-120, 120, pad.tune);
        p.voiceOverlap = toOverlap (pad.mode);
        out.pads.push_back (std::move (p));
    }

    return out;
}

std::optional<Bank> importProgram (const juce::File& pgmFile, juce::StringArray& missing, juce::String& error)
{
    std::string why;
    auto program = mpc::readPgm (readBytes (pgmFile), &why);

    if (! program)
    {
        error = pgmFile.getFileName() + ": " + juce::String (why);
        return std::nullopt;
    }

    Bank bank;
    bank.name = program->name.empty() ? pgmFile.getFileNameWithoutExtension() : juce::String (program->name);

    // Sounds are files next to the program, matched without case and ignoring spaces.
    std::map<juce::String, juce::File> filesByName;
    for (const auto& f : pgmFile.getParentDirectory().findChildFiles (juce::File::findFiles, false, "*"))
        if (f.hasFileExtension ("snd"))
            filesByName[f.getFileNameWithoutExtension().removeCharacters (" ").toUpperCase()] = f;

    std::vector<SharedAudio> sounds;
    for (const auto& name : program->soundNames)
    {
        auto key = juce::String (name).removeCharacters (" ").toUpperCase();
        SharedAudio audio;

        if (auto it = filesByName.find (key); it != filesByName.end())
        {
            juce::AudioFormatManager none;
            juce::String loadError;
            audio = load (it->second, none, loadError);
        }

        if (audio == nullptr)
            missing.add (name);

        sounds.push_back (audio);
    }

    for (int i = 0; i < mpc::kPadCount; ++i)
    {
        const auto& note = program->noteForPad (i);
        const auto& mixer = program->mixers[(size_t) (program->padToNote[(size_t) i] - mpc::kFirstNote)];

        if (note.soundIndex >= sounds.size() || sounds[note.soundIndex] == nullptr)
            continue;

        auto& pad = bank.pads[(size_t) i];
        pad.audio = sounds[note.soundIndex];
        pad.name = program->soundNames[note.soundIndex];
        pad.volume = mixer.volume;
        pad.pan = mixer.pan;
        pad.tune = note.tune;
        pad.mode = fromOverlap (note.voiceOverlap);
        pad.mono = pad.audio->getNumChannels() == 1;
    }

    return bank;
}

juce::String formatMegabytes (size_t bytes)
{
    return juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB";
}

} // namespace SampleTools
