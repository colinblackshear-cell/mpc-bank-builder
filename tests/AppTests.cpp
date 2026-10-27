// Headless test of the app's audio side: decode and resample a WAV, render pads, export a kit
// folder, and open it again.

#include <juce_audio_formats/juce_audio_formats.h>

#include "../Source/app/SampleTools.h"
#include "../Source/app/SoundFx.h"
#include "../Source/app/SoundLibrary.h"

#include <iostream>

static int failures = 0;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (! (cond)) {                                                                  \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n";   \
            ++failures;                                                                  \
        }                                                                                \
    } while (0)

static juce::File writeTestWav (const juce::File& dir, const juce::String& name, double rate, int channels, int length)
{
    auto file = dir.getChildFile (name + ".wav");
    juce::AudioBuffer<float> buffer (channels, length);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < length; ++i)
            buffer.setSample (ch, i, 0.5f * std::sin ((float) i * 0.05f * (float) (ch + 1)));

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (rate)
                                                   .withNumChannels (channels)
                                                   .withBitsPerSample (16));
    if (writer != nullptr)
        writer->writeFromAudioSampleBuffer (buffer, 0, length);
    return file;
}

static bool same (double a, double b) { return std::abs (a - b) < 1.0e-9; }

// Clicks every `gap` samples on silence: easy hits to find.
static juce::AudioBuffer<float> clicks (int count, int gap)
{
    juce::AudioBuffer<float> b (1, count * gap);
    b.clear();
    for (int c = 0; c < count; ++c)
        for (int i = 0; i < 400; ++i)
            b.setSample (0, c * gap + i, 0.8f * std::exp (-(float) i / 80.0f) * std::sin ((float) i * 0.3f));
    return b;
}

static void testSoundFx()
{
    // stretching changes length; pitch shifting keeps it
    juce::AudioBuffer<float> tone (2, 44100);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 44100; ++i)
            tone.setSample (ch, i, 0.5f * std::sin ((float) i * 0.06f));
    CHECK (SoundFx::stretch (tone, 22050).getNumSamples() == 22050);
    CHECK (SoundFx::stretch (tone, 88200).getNumSamples() == 88200);
    auto shifted = SoundFx::stretch (tone, 44100, 7.0f);
    CHECK (shifted.getNumSamples() == 44100 && shifted.getMagnitude (0, 44100) > 0.1f);
    // very short sounds fall back to resampling instead of failing
    CHECK (SoundFx::stretch (SoundFx::range (tone, 0, 300), 600).getNumSamples() == 600);

    SoundFx::Settings neutral;
    CHECK (neutral.isNeutral());
    CHECK (SoundFx::apply (tone, neutral).getNumSamples() == 44100);

    SoundFx::Settings fx;
    fx.speed = 2.0f;
    fx.bits = 6;
    fx.sampleRate = 8000;
    fx.drive = 0.5f;
    fx.highCut = 3000;
    fx.lowCut = 100;
    auto crushed = SoundFx::apply (tone, fx);
    CHECK (std::abs (crushed.getNumSamples() - 22050) <= 1);
    CHECK (crushed.getMagnitude (0, crushed.getNumSamples()) <= 1.0f);

    // delay and reverb add a tail, then stop at silence
    SoundFx::Settings wet;
    wet.delayMix = 0.5f;
    wet.reverbMix = 0.4f;
    auto tail = SoundFx::apply (clicks (1, 4410), wet);
    CHECK (tail.getNumSamples() > 4410);
    CHECK (tail.getNumSamples() <= 4410 + (int) (4 * kMpcSampleRate));

    // edits
    CHECK (SoundFx::range (tone, 100, 300).getNumSamples() == 200);
    CHECK (SoundFx::removeRange (tone, 100, 300).getNumSamples() == 44100 - 200);
    auto withSilence = SoundFx::removeRange (clicks (1, 20000), 0, 0);
    auto audible = SoundFx::audibleRange (withSilence);
    CHECK (audible.getStart() == 0 && audible.getEnd() < 2000);

    // chopping: 8 clicks a quarter second apart give 7 markers near the right places
    auto beat = clicks (8, 11025);
    auto hits = SoundFx::findHits (beat, 0.5f);
    CHECK (hits.size() == 7);
    for (size_t i = 0; i < hits.size() && i < 7; ++i)
        CHECK (std::abs (hits[i] - (int) (i + 1) * 11025) < 600);
    auto slices = SoundFx::slice (beat, hits);
    CHECK (slices.size() == 8);
    CHECK (SoundFx::gridMarkers (1000, 4) == std::vector<int> ({ 250, 500, 750 }));
    CHECK (SoundFx::slice (beat, SoundFx::gridMarkers (beat.getNumSamples(), 16)).size() == 16);

    // tempo: a 2 second loop at 120 BPM is 4 beats; at 90 BPM it's still best read as 4 beats
    CHECK (same (SoundFx::guessBeats (88200, 120.0), 4.0));
    CHECK (same (SoundFx::guessBeats (88200, 90.0), 4.0));
    CHECK (same (SoundFx::guessBeats (4410, 120.0), 0.0));   // a 0.1 s hit plays normal
    CHECK (std::abs (SoundFx::tempoOf (88200, 4.0) - 120.0) < 0.01);
    auto loop = std::make_shared<juce::AudioBuffer<float>> (tone);   // 1 s
    auto fitted = SoundFx::fitToTempo (loop, 2.0, 100.0);            // 2 beats at 100 BPM = 1.2 s
    CHECK (std::abs (fitted->getNumSamples() - 52920) <= 1);
}

static void testTempoKit (const juce::File& dir)
{
    Bank bank;
    bank.name = "Tempo";
    bank.bpm = 100.0;
    auto loop = std::make_shared<juce::AudioBuffer<float>> (clicks (4, 11025));   // 1 s, 4 hits
    bank.pads[0].audio = loop;
    bank.pads[0].name = "loop";
    bank.pads[1].audio = loop;
    bank.pads[1].name = "free";
    bank.pads[1].playNormal = true;
    bank.pads[2].audio = std::make_shared<juce::AudioBuffer<float>> (clicks (1, 2000));
    bank.pads[2].name = "hit";

    // off: everything plays as recorded
    CHECK (bank.audioToPlay (0) == loop);
    CHECK (same (bank.beatsFor (0), 0.0));

    bank.keepInTime = true;
    CHECK (same (bank.beatsFor (0), 2.0));         // 1 s at 100 BPM is closest to 2 beats
    CHECK (same (bank.beatsFor (1), 0.0));         // play normal
    CHECK (same (bank.beatsFor (2), 0.0));         // short hit
    CHECK (std::abs (bank.audioToPlay (0)->getNumSamples() - 52920) <= 1);
    CHECK (bank.audioToPlay (0) == bank.audioToPlay (0));   // cached
    CHECK (bank.audioToPlay (1) == loop);
    CHECK (bank.memoryBytes (0) == 52920 * 2);

    bank.pads[0].beats = 4.0;                 // set by hand
    CHECK (std::abs (bank.audioToPlay (0)->getNumSamples() - 105840) <= 1);

    std::vector<std::string> taken;
    auto files = mpc::buildBank (SampleTools::toMpcBank (bank), taken);
    CHECK (files.sounds.size() == 3);
    if (files.sounds.size() == 3)
    {
        CHECK (std::abs ((int) files.sounds[0].frameCount() - 105840) <= 1);
        CHECK (files.sounds[0].beatCount == 4);
        CHECK (files.sounds[1].frameCount() == 44100);
    }

    // the sound library is a folder of WAVs
    auto libDir = dir.getChildFile ("library");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    SoundLibrary library (formats, libDir);
    CHECK (library.getEntries().empty());
    auto saved = library.save (*loop, "My Loop");
    CHECK (saved.existsAsFile() && saved.getFileName() == "My Loop.wav");
    auto second = library.save (*loop, "My Loop");
    CHECK (second.getFileName() != saved.getFileName());
    CHECK (library.getEntries().size() == 2);
    juce::String error;
    auto back = SampleTools::load (saved, formats, error);
    CHECK (back != nullptr && back->getNumSamples() == loop->getNumSamples());
    CHECK (library.rename (library.getEntries().back(), "Renamed"));
    CHECK (libDir.getChildFile ("Renamed.wav").existsAsFile());
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mpc-bank-builder-app-test");
    dir.deleteRecursively();
    dir.createDirectory();

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    // A 48 kHz stereo file is resampled to 44.1 kHz.
    auto stereoFile = writeTestWav (dir, "Kick Drum", 48000.0, 2, 48000);
    juce::String error;
    auto stereo = SampleTools::load (stereoFile, formats, error);
    CHECK (stereo != nullptr);
    if (stereo == nullptr)
    {
        std::cerr << error << "\n";
        return 1;
    }
    CHECK (stereo->getNumChannels() == 2);
    CHECK (std::abs (stereo->getNumSamples() - 44100) <= 1);

    auto monoFile = writeTestWav (dir, "snare", 44100.0, 1, 1000);
    auto mono = SampleTools::load (monoFile, formats, error);
    CHECK (mono != nullptr && mono->getNumChannels() == 1 && mono->getNumSamples() == 1000);

    CHECK (SampleTools::expandDroppedFiles ({ dir.getFullPathName() }, formats).size() == 2);

    Bank bank;
    bank.name = "Test Kit";
    bank.pads[0].audio = stereo;
    bank.pads[0].name = "Kick Drum";
    bank.pads[0].tune = -25;
    bank.pads[1].audio = stereo;
    bank.pads[1].name = "Kick Drum";
    bank.pads[1].mono = true;
    bank.pads[1].reverse = true;
    bank.pads[17].audio = mono;
    bank.pads[17].name = "snare";
    bank.pads[17].mode = PadMode::noteOff;
    bank.pads[17].normalize = true;

    CHECK (bank.usedPads() == 3);
    CHECK (bank.memoryBytes() == (size_t) stereo->getNumSamples() * 6 + 2000);

    // reverse and normalize are baked into the saved sound
    auto reversed = SampleTools::renderPad (bank.pads[1]);
    CHECK (! reversed.isStereo());
    auto normalized = SampleTools::renderPad (bank.pads[17]);
    int16_t peak = 0;
    for (auto s : normalized.left)
        peak = std::max<int16_t> (peak, (int16_t) std::abs (s));
    CHECK (peak > 31000);

    std::vector<std::string> taken;
    auto files = mpc::buildBank (SampleTools::toMpcBank (bank), taken);
    auto out = dir.getChildFile ("card");
    auto result = mpc::writeBankFolder (out.getFullPathName().toStdString(), files);
    CHECK (result.ok);

    auto pgm = out.getChildFile ("TESTKIT").getChildFile ("TESTKIT.PGM");
    CHECK (pgm.existsAsFile());
    CHECK (out.getChildFile ("TESTKIT").getChildFile ("KICKDRUM.SND").existsAsFile());
    CHECK (out.getChildFile ("TESTKIT").getChildFile ("KICKDR_2.SND").existsAsFile());
    CHECK (out.getChildFile ("TESTKIT").getChildFile ("SNARE.SND").existsAsFile());

    juce::StringArray missing;
    auto opened = SampleTools::importProgram (pgm, missing, error);
    CHECK (opened.has_value());
    CHECK (missing.isEmpty());
    if (opened)
    {
        CHECK (opened->name == "Test Kit");
        CHECK (opened->usedPads() == 3);
        CHECK (opened->pads[0].tune == -25);
        CHECK (opened->pads[0].audio->getNumChannels() == 2);
        CHECK (opened->pads[1].audio->getNumChannels() == 1);
        CHECK (opened->pads[17].mode == PadMode::noteOff);
        CHECK (opened->pads[17].audio->getNumSamples() == 1000);
    }

    CHECK (padLabel (0) == "A01" && padLabel (17) == "B02" && padLabel (63) == "D16");
    CHECK (noteForPad (0) == 37 && padForNote (37) == 0 && padForNote (20) == -1);

    testSoundFx();
    testTempoKit (dir);

    dir.deleteRecursively();

    if (failures == 0)
        std::cout << "All app tests passed\n";
    return failures == 0 ? 0 : 1;
}
