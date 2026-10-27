// Headless test of the app's audio side: decode and resample a WAV, render pads, export a kit
// folder, and open it again.

#include <juce_audio_formats/juce_audio_formats.h>

#include "../Source/app/SampleTools.h"

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

    dir.deleteRecursively();

    if (failures == 0)
        std::cout << "All app tests passed\n";
    return failures == 0 ? 0 : 1;
}
