#include "MpcFiles.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace mpc
{

namespace
{
    const std::string kAllowedSpecials = "_^$~!#%&-{}()@`";

    struct Writer
    {
        Bytes out;

        void u8 (uint8_t v) { out.push_back (v); }
        void s8 (int8_t v) { out.push_back ((uint8_t) v); }
        void u16 (uint16_t v) { u8 ((uint8_t) (v & 0xFF)); u8 ((uint8_t) (v >> 8)); }
        void s16 (int16_t v) { u16 ((uint16_t) v); }
        void u32 (uint32_t v) { u16 ((uint16_t) (v & 0xFFFF)); u16 ((uint16_t) (v >> 16)); }

        template <size_t N>
        void raw (const std::array<uint8_t, N>& a) { out.insert (out.end(), a.begin(), a.end()); }

        // A 16-character name padded with spaces, then a terminating zero.
        void name (const std::string& n)
        {
            for (int i = 0; i < kNameLength; ++i)
                u8 (i < (int) n.size() ? (uint8_t) n[(size_t) i] : (uint8_t) ' ');
            u8 (0);
        }
    };

    struct Reader
    {
        const Bytes& in;
        size_t pos = 0;
        bool failed = false;

        bool has (size_t n)
        {
            if (pos + n > in.size())
                failed = true;
            return ! failed;
        }

        uint8_t u8() { return has (1) ? in[pos++] : 0; }
        int8_t s8() { return (int8_t) u8(); }
        uint16_t u16() { auto lo = u8(); auto hi = u8(); return (uint16_t) (lo | (hi << 8)); }
        int16_t s16() { return (int16_t) u16(); }
        uint32_t u32() { uint32_t lo = u16(); uint32_t hi = u16(); return lo | (hi << 16); }

        template <size_t N>
        void raw (std::array<uint8_t, N>& a)
        {
            for (auto& b : a)
                b = u8();
        }

        std::string name()
        {
            std::string n;
            for (int i = 0; i < kNameLength + 1; ++i)
                n.push_back ((char) u8());
            n.erase (std::find (n.begin(), n.end(), '\0'), n.end());
            while (! n.empty() && n.back() == ' ')
                n.pop_back();
            return n;
        }
    };

    template <typename T>
    std::optional<T> fail (std::string* error, const std::string& message)
    {
        if (error != nullptr)
            *error = message;
        return std::nullopt;
    }
}

//==============================================================================
std::string toMpcName (const std::string& name, int maxLength)
{
    std::string result;

    for (char c : name)
    {
        if ((int) result.size() >= maxLength)
            break;

        auto u = (char) std::toupper ((unsigned char) c);

        if (u == ' ' || u == '.')
            continue;

        if ((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || kAllowedSpecials.find (u) != std::string::npos)
            result.push_back (u);
        else
            result.push_back ('_');
    }

    return result.empty() ? "SOUND" : result;
}

std::string makeUniqueName (const std::string& name, std::vector<std::string>& taken, int maxLength)
{
    auto base = toMpcName (name, maxLength);

    auto isTaken = [&taken] (const std::string& candidate)
    {
        return std::any_of (taken.begin(), taken.end(), [&candidate] (const std::string& t)
        {
            return t.size() == candidate.size()
                && std::equal (t.begin(), t.end(), candidate.begin(),
                               [] (char a, char b) { return std::toupper ((unsigned char) a) == std::toupper ((unsigned char) b); });
        });
    };

    auto candidate = base;

    for (int n = 2; isTaken (candidate); ++n)
    {
        auto suffix = std::to_string (n);
        auto keep = std::min ((int) base.size(), maxLength - (int) suffix.size());

        // "BREAKBEAT" -> "BREAKB_2" reads better than "BREAKBE2" once the name is cut short.
        if (keep < (int) base.size() && keep > 1)
            candidate = base.substr (0, (size_t) keep - 1) + "_" + suffix;
        else
            candidate = base.substr (0, (size_t) keep) + suffix;
    }

    taken.push_back (candidate);
    return candidate;
}

//==============================================================================
Bytes writeSnd (const Sound& sound)
{
    Writer w;
    const auto frames = sound.frameCount();
    const auto end = std::min (sound.end == 0 ? frames : sound.end, frames);
    const auto loopTo = std::min (sound.loopTo, end);

    w.u8 (0x01);
    w.u8 (0x04);
    w.name (sound.name);
    w.u8 (sound.level);
    w.s8 (sound.tune);
    w.u8 (sound.isStereo() ? 1 : 0);
    w.u32 (std::min (sound.start, end));
    w.u32 (end);
    w.u32 (frames);
    w.u32 (end - loopTo);   // stored as the loop length, counted back from the end
    w.u8 (sound.loopEnabled ? 1 : 0);
    w.u8 (sound.beatCount);
    w.u16 (sound.sampleRate);

    w.out.reserve (w.out.size() + sound.memoryBytes());

    for (auto s : sound.left)
        w.s16 (s);

    if (sound.isStereo())
        for (uint32_t i = 0; i < frames; ++i)
            w.s16 (i < sound.right.size() ? sound.right[i] : 0);

    return std::move (w.out);
}

std::optional<Sound> readSnd (const Bytes& data, std::string* error)
{
    if (data.size() < 42)
        return fail<Sound> (error, "File is too short to be a sound");

    if (data[0] != 0x01 || data[1] != 0x04)
        return fail<Sound> (error, "Not an MPC2000XL sound (it may be from an MPC60 or MPC3000)");

    Reader r { data };
    r.pos = 2;

    Sound s;
    s.name = r.name();
    s.level = r.u8();
    s.tune = r.s8();
    const bool stereo = r.u8() != 0;
    s.start = r.u32();
    s.end = r.u32();
    const auto frames = r.u32();
    const auto loopLength = r.u32();
    s.loopEnabled = r.u8() != 0;
    s.beatCount = r.u8();
    s.sampleRate = r.u16();
    s.loopTo = s.end >= loopLength ? s.end - loopLength : 0;

    const size_t channels = stereo ? 2 : 1;

    if (data.size() - r.pos < (size_t) frames * channels * 2)
        return fail<Sound> (error, "Sound data is cut short");

    s.left.resize (frames);
    for (auto& v : s.left)
        v = r.s16();

    if (stereo)
    {
        s.right.resize (frames);
        for (auto& v : s.right)
            v = r.s16();
    }

    return s;
}

//==============================================================================
std::array<uint8_t, kPadCount> Program::defaultPadToNote()
{
    return {{ 37, 36, 42, 82, 40, 38, 46, 44, 48, 47, 45, 43, 49, 55, 51, 53,
              54, 69, 81, 80, 65, 66, 76, 77, 56, 62, 63, 64, 73, 74, 71, 39,
              52, 57, 58, 59, 60, 61, 67, 68, 70, 72, 75, 78, 79, 35, 41, 50,
              83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98 }};
}

std::array<uint8_t, 200> Program::defaultEffects()
{
    return {{
        0x02, 0x00, 0x48, 0x00, 0xd0, 0x07, 0x00, 0x00, 0x63, 0x01, 0x14, 0x08, 0x1d, 0xfc, 0x32, 0x33,
        0x02, 0x32, 0x3c, 0x08, 0x05, 0x0a, 0x14, 0x14, 0x32, 0x00, 0x00, 0x02, 0x0f, 0x19, 0x00, 0x05,
        0x41, 0x14, 0x1e, 0x01, 0x05, 0x00, 0x00, 0x05, 0x63, 0x00, 0xf4, 0xff, 0x0c, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x4f, 0x01, 0x4f, 0x01, 0x00, 0x42, 0x4f, 0x01, 0x00, 0x42,
        0x4f, 0x01, 0x00, 0x42, 0x32, 0x00, 0x63, 0x28, 0x00, 0x3c, 0x00, 0x00, 0xd0, 0x07, 0x00, 0x00,
        0x63, 0x01, 0x14, 0x08, 0x1d, 0xfc, 0x32, 0x33, 0x02, 0x32, 0x3c, 0x08, 0x05, 0x0a, 0x14, 0x14,
        0x32, 0x00, 0x00, 0x02, 0x0f, 0x19, 0x00, 0x05, 0x41, 0x14, 0x1e, 0x01, 0x05, 0x00, 0x00, 0x05,
        0x63, 0x00, 0xf4, 0xff, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x4f, 0x01,
        0x4f, 0x01, 0x00, 0x42, 0x4f, 0x01, 0x00, 0x42, 0x4f, 0x01, 0x00, 0x42, 0x32, 0x00, 0x63, 0x28,
        0x00, 0x3c, 0x00, 0x00, 0x04, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x32, 0x00, 0x23, 0x00, 0x3e, 0x33,
        0x5a, 0x32, 0x14, 0x00, 0x00, 0x00, 0x32, 0x00, 0x23, 0x00, 0x3e, 0x33, 0x5a, 0x32, 0x14, 0x00,
        0x00, 0x00, 0x32, 0x00, 0x23, 0x00, 0x3e, 0x33, 0x5a, 0x32, 0x14, 0x00, 0x00, 0x00, 0x32, 0x00,
        0x23, 0x00, 0x3e, 0x33, 0x5a, 0x32, 0x14, 0x00 }};
}

Bytes writePgm (const Program& p)
{
    Writer w;

    w.u8 (0x07);
    w.u8 (0x04);
    w.u16 ((uint16_t) p.soundNames.size());
    for (const auto& n : p.soundNames)
        w.name (n);

    w.raw (p.header);
    w.name (p.name);
    w.raw (p.slider);
    w.u8 (p.programChange);
    w.raw (p.afterProgramChange);

    for (const auto& n : p.notes)
    {
        w.u8 (n.soundIndex);
        w.u8 (n.soundGenerationMode);
        w.u8 (n.velocityRangeLower);
        w.u8 (n.alsoPlayNote1);
        w.u8 (n.velocityRangeUpper);
        w.u8 (n.alsoPlayNote2);
        w.u8 ((uint8_t) n.voiceOverlap);
        w.u8 (n.muteNote1);
        w.u8 (n.muteNote2);
        w.s16 (n.tune);
        w.u8 (n.attack);
        w.u8 (n.decay);
        w.u8 (n.decayMode);
        w.u8 (n.cutoff);
        w.u8 (n.resonance);
        w.u8 (n.filterEnvAttack);
        w.u8 (n.filterEnvDecay);
        w.u8 (n.filterEnvAmount);
        w.u8 (n.velocityToLevel);
        w.u8 (n.velocityToAttack);
        w.u8 (n.velocityToStart);
        w.u8 (n.velocityToCutoff);
        w.u8 (n.sliderParameter);
        w.u8 (n.velocityToPitch);
    }

    w.u8 (p.afterNotes);

    for (const auto& m : p.mixers)
    {
        w.u8 (m.fxOutput);
        w.u8 (m.volume);
        w.u8 (m.pan);
        w.u8 (m.volumeIndividual);
        w.u8 (m.output);
        w.u8 (m.fxSendLevel);
    }

    w.raw (p.afterMixers);
    w.raw (p.padToNote);
    w.raw (p.effects);

    return std::move (w.out);
}

std::optional<Program> readPgm (const Bytes& data, std::string* error)
{
    if (data.size() < 4 || data[0] != 0x07 || data[1] != 0x04)
        return fail<Program> (error, "Not an MPC2000XL program");

    Reader r { data };
    r.pos = 2;

    Program p;
    const auto soundCount = r.u16();

    if (data.size() != 2290 + (size_t) soundCount * 17)
        return fail<Program> (error, "Program file has an unexpected size");

    for (int i = 0; i < soundCount; ++i)
        p.soundNames.push_back (r.name());

    r.raw (p.header);
    p.name = r.name();
    r.raw (p.slider);
    p.programChange = r.u8();
    r.raw (p.afterProgramChange);

    for (auto& n : p.notes)
    {
        n.soundIndex = r.u8();
        n.soundGenerationMode = r.u8();
        n.velocityRangeLower = r.u8();
        n.alsoPlayNote1 = r.u8();
        n.velocityRangeUpper = r.u8();
        n.alsoPlayNote2 = r.u8();
        n.voiceOverlap = (VoiceOverlap) r.u8();
        n.muteNote1 = r.u8();
        n.muteNote2 = r.u8();
        n.tune = r.s16();
        n.attack = r.u8();
        n.decay = r.u8();
        n.decayMode = r.u8();
        n.cutoff = r.u8();
        n.resonance = r.u8();
        n.filterEnvAttack = r.u8();
        n.filterEnvDecay = r.u8();
        n.filterEnvAmount = r.u8();
        n.velocityToLevel = r.u8();
        n.velocityToAttack = r.u8();
        n.velocityToStart = r.u8();
        n.velocityToCutoff = r.u8();
        n.sliderParameter = r.u8();
        n.velocityToPitch = r.u8();
    }

    p.afterNotes = r.u8();

    for (auto& m : p.mixers)
    {
        m.fxOutput = r.u8();
        m.volume = r.u8();
        m.pan = r.u8();
        m.volumeIndividual = r.u8();
        m.output = r.u8();
        m.fxSendLevel = r.u8();
    }

    r.raw (p.afterMixers);
    r.raw (p.padToNote);
    r.raw (p.effects);

    for (auto note : p.padToNote)
        if (note < kFirstNote || note >= kFirstNote + kPadCount)
            return fail<Program> (error, "Program has a pad mapped to an invalid note");

    if (r.failed)
        return fail<Program> (error, "Program file is cut short");

    return p;
}

} // namespace mpc
