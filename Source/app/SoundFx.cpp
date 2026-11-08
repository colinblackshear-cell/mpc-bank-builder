#include "SoundFx.h"

#include <juce_dsp/juce_dsp.h>
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace SoundFx
{

namespace
{
    constexpr float kTailSeconds = 4.0f;

    // Pointers to each channel, in the shape the stretcher wants.
    struct Channels
    {
        explicit Channels (juce::AudioBuffer<float>& b)
        {
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                pointers.push_back (b.getWritePointer (ch));
        }

        float* operator[] (int ch) const { return pointers[(size_t) ch]; }
        std::vector<float*> pointers;
    };

    juce::AudioBuffer<float> withTail (const juce::AudioBuffer<float>& in, int tail)
    {
        juce::AudioBuffer<float> out (in.getNumChannels(), in.getNumSamples() + tail);
        out.clear();
        for (int ch = 0; ch < in.getNumChannels(); ++ch)
            out.copyFrom (ch, 0, in, ch, 0, in.getNumSamples());
        return out;
    }

    // Cuts silence after `keepAtLeast` samples.
    void trimTail (juce::AudioBuffer<float>& b, int keepAtLeast)
    {
        int end = b.getNumSamples();
        while (end > keepAtLeast && b.getMagnitude (end - 1, 1) < 0.0005f)
            --end;
        b.setSize (b.getNumChannels(), juce::jmax (1, end), true, false, true);
    }
}

juce::AudioBuffer<float> stretch (const juce::AudioBuffer<float>& in, int newLength, float semitones)
{
    const int channels = in.getNumChannels();
    newLength = juce::jmax (1, newLength);

    if (newLength == in.getNumSamples() && std::abs (semitones) < 0.01f)
        return in;

    signalsmith::stretch::SignalsmithStretch<float> stretcher;
    stretcher.presetDefault (channels, (float) kMpcSampleRate);
    stretcher.setTransposeSemitones (semitones);

    juce::AudioBuffer<float> source (in);
    juce::AudioBuffer<float> out (channels, newLength);
    out.clear();

    Channels inputs (source), outputs (out);

    if (! stretcher.exact (inputs, in.getNumSamples(), outputs, newLength))
    {
        // Too short for the phase vocoder (under ~0.1 s): resample instead, which changes
        // pitch along with length, but such short sounds are hits where that's fine.
        const double ratio = (double) in.getNumSamples() / newLength;
        for (int ch = 0; ch < channels; ++ch)
        {
            std::vector<float> padded ((size_t) in.getNumSamples() + 64, 0.0f);
            std::copy (in.getReadPointer (ch), in.getReadPointer (ch) + in.getNumSamples(), padded.begin());
            juce::LagrangeInterpolator interpolator;
            interpolator.process (ratio, padded.data(), out.getWritePointer (ch), newLength);
        }
    }

    return out;
}

bool Settings::isNeutral() const
{
    return std::abs (pitch) < 0.01f && std::abs (speed - 1.0f) < 0.001f && lowCut <= 20.0f && highCut >= 20000.0f
        && bits >= 16 && sampleRate >= 44100.0f && drive <= 0.0f && delayMix <= 0.0f && reverbMix <= 0.0f;
}

juce::AudioBuffer<float> apply (const juce::AudioBuffer<float>& in, const Settings& s)
{
    if (s.isNeutral() || in.getNumSamples() == 0)
        return in;

    const int channels = in.getNumChannels();

    // 1. pitch and speed
    auto out = stretch (in, (int) std::round (in.getNumSamples() / juce::jlimit (0.25f, 4.0f, s.speed)), s.pitch);
    const int dryLength = out.getNumSamples();

    // delay and reverb ring on past the end
    if (s.delayMix > 0.0f || s.reverbMix > 0.0f)
        out = withTail (out, (int) (kTailSeconds * kMpcSampleRate));

    const int n = out.getNumSamples();
    juce::dsp::ProcessSpec spec { kMpcSampleRate, (juce::uint32) n, (juce::uint32) channels };

    // 2. filters
    const float q = 0.707f + s.resonance * 8.0f;
    if (s.lowCut > 20.0f)
    {
        juce::dsp::StateVariableTPTFilter<float> filter;
        filter.prepare (spec);
        filter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        filter.setCutoffFrequency (juce::jlimit (20.0f, 18000.0f, s.lowCut));
        filter.setResonance (q);
        juce::dsp::AudioBlock<float> block (out);
        filter.process (juce::dsp::ProcessContextReplacing<float> (block));
    }
    if (s.highCut < 20000.0f)
    {
        juce::dsp::StateVariableTPTFilter<float> filter;
        filter.prepare (spec);
        filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        filter.setCutoffFrequency (juce::jlimit (40.0f, 20000.0f, s.highCut));
        filter.setResonance (q);
        juce::dsp::AudioBlock<float> block (out);
        filter.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    // 3. drive: soft clipping with make-up gain so louder settings don't just get louder
    if (s.drive > 0.0f)
    {
        const float gain = 1.0f + s.drive * 20.0f;
        const float makeup = 1.0f / std::tanh (gain);
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* d = out.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                d[i] = std::tanh (d[i] * gain) * makeup;
        }
    }

    // 4. crush: fewer bits and a lower sample rate (sample and hold), like old samplers
    if (s.bits < 16 || s.sampleRate < 44100.0f)
    {
        const float levels = std::pow (2.0f, (float) juce::jlimit (2, 16, s.bits) - 1.0f);
        const double hold = kMpcSampleRate / juce::jlimit (500.0f, 44100.0f, s.sampleRate);
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* d = out.getWritePointer (ch);
            double phase = 0;
            float held = 0;
            for (int i = 0; i < n; ++i)
            {
                if ((phase += 1.0) >= hold || i == 0)
                {
                    phase -= hold;
                    held = std::round (d[i] * levels) / levels;
                }
                d[i] = held;
            }
        }
    }

    // 5. delay, with the repeats ping-ponging between channels on stereo sounds
    if (s.delayMix > 0.0f)
    {
        const int delaySamples = juce::jmax (1, (int) (juce::jlimit (0.01f, 2.0f, s.delayTime) * kMpcSampleRate));
        juce::AudioBuffer<float> wet (channels, n);
        wet.clear();
        const float feedback = juce::jlimit (0.0f, 0.95f, s.delayFeedback);
        for (int i = delaySamples; i < n; ++i)
            for (int ch = 0; ch < channels; ++ch)
            {
                const int from = channels == 2 ? 1 - ch : ch;
                wet.setSample (ch, i, out.getSample (from, i - delaySamples) * (1.0f - feedback * 0.5f)
                                          + wet.getSample (from, i - delaySamples) * feedback);
            }
        for (int ch = 0; ch < channels; ++ch)
            out.addFrom (ch, 0, wet, ch, 0, n, s.delayMix);
    }

    // 6. reverb
    if (s.reverbMix > 0.0f)
    {
        juce::Reverb reverb;
        juce::Reverb::Parameters p;
        p.roomSize = juce::jlimit (0.0f, 1.0f, s.reverbSize);
        p.damping = 0.4f;
        p.wetLevel = s.reverbMix;
        p.dryLevel = 1.0f - s.reverbMix * 0.5f;
        p.width = 1.0f;
        reverb.setParameters (p);
        reverb.setSampleRate (kMpcSampleRate);
        if (channels == 2)
            reverb.processStereo (out.getWritePointer (0), out.getWritePointer (1), n);
        else
            reverb.processMono (out.getWritePointer (0), n);
    }

    trimTail (out, dryLength);

    // never clip on the way to 16 bits
    if (out.getMagnitude (0, out.getNumSamples()) > 1.0f)
        normalize (out);

    return out;
}

juce::AudioBuffer<float> range (const juce::AudioBuffer<float>& in, int start, int end)
{
    start = juce::jlimit (0, in.getNumSamples(), start);
    end = juce::jlimit (start, in.getNumSamples(), end);
    juce::AudioBuffer<float> out (in.getNumChannels(), juce::jmax (1, end - start));
    out.clear();
    for (int ch = 0; ch < in.getNumChannels(); ++ch)
        out.copyFrom (ch, 0, in, ch, start, end - start);
    return out;
}

juce::AudioBuffer<float> removeRange (const juce::AudioBuffer<float>& in, int start, int end)
{
    start = juce::jlimit (0, in.getNumSamples(), start);
    end = juce::jlimit (start, in.getNumSamples(), end);
    const int remaining = in.getNumSamples() - (end - start);
    juce::AudioBuffer<float> out (in.getNumChannels(), juce::jmax (1, remaining));
    out.clear();
    for (int ch = 0; ch < in.getNumChannels(); ++ch)
    {
        out.copyFrom (ch, 0, in, ch, 0, start);
        out.copyFrom (ch, start, in, ch, end, in.getNumSamples() - end);
    }
    return out;
}

void fade (juce::AudioBuffer<float>& b, int start, int end, bool fadeIn)
{
    start = juce::jlimit (0, b.getNumSamples(), start);
    end = juce::jlimit (start, b.getNumSamples(), end);
    if (end > start)
        b.applyGainRamp (start, end - start, fadeIn ? 0.0f : 1.0f, fadeIn ? 1.0f : 0.0f);
}

void normalize (juce::AudioBuffer<float>& b)
{
    const auto peak = b.getMagnitude (0, b.getNumSamples());
    if (peak > 0.0001f)
        b.applyGain (0.98f / peak);
}

juce::Range<int> audibleRange (const juce::AudioBuffer<float>& b, float threshold)
{
    int start = 0, end = b.getNumSamples();
    while (start < end && b.getMagnitude (start, 1) < threshold)
        ++start;
    while (end > start && b.getMagnitude (end - 1, 1) < threshold)
        --end;
    if (start >= end)
        return { 0, b.getNumSamples() };
    // keep a few ms before the first sound so the attack isn't cut
    return { juce::jmax (0, start - 64), end };
}

std::vector<int> findHits (const juce::AudioBuffer<float>& b, float sensitivity, int maxHits)
{
    // Energy-based onset detection: compare each short frame's energy with the average of the
    // frames before it; a big jump is a hit.
    constexpr int frame = 512;
    const int frames = b.getNumSamples() / frame;
    std::vector<float> energy ((size_t) frames, 0.0f);

    for (int f = 0; f < frames; ++f)
    {
        float sum = 0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const auto* d = b.getReadPointer (ch, f * frame);
            for (int i = 0; i < frame; ++i)
                sum += d[i] * d[i];
        }
        energy[(size_t) f] = sum / (float) (frame * b.getNumChannels());
    }

    const float ratio = juce::jmap (juce::jlimit (0.0f, 1.0f, sensitivity), 8.0f, 1.6f);
    const float floor = 1.0e-5f;
    const int minGapFrames = (int) (0.06 * kMpcSampleRate / frame);   // at most ~16 hits a second

    struct Hit { int position; float strength; };
    std::vector<Hit> hits;
    int lastHit = -minGapFrames;

    for (int f = 1; f < frames; ++f)
    {
        float before = 0;
        const int window = juce::jmin (f, 8);
        for (int k = 1; k <= window; ++k)
            before += energy[(size_t) (f - k)];
        before = before / (float) window;

        if (energy[(size_t) f] > floor && energy[(size_t) f] > before * ratio && f - lastHit >= minGapFrames)
        {
            // Find the start of the attack inside the frame before the jump.
            int position = (f - 1) * frame;
            const auto* d = b.getReadPointer (0);
            const float level = std::sqrt (energy[(size_t) f]) * 0.25f;
            for (int i = (f - 1) * frame; i < (f + 1) * frame && i < b.getNumSamples(); ++i)
                if (std::abs (d[i]) > level)
                {
                    position = i;
                    break;
                }

            hits.push_back ({ juce::jmax (0, position - 32), energy[(size_t) f] / (before + floor) });
            lastHit = f;
        }
    }

    // Keep the strongest if there are too many, then put them back in order.
    if ((int) hits.size() > maxHits)
    {
        std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& c) { return a.strength > c.strength; });
        hits.resize ((size_t) maxHits);
    }

    std::vector<int> markers;
    for (const auto& h : hits)
        if (h.position > (int) (0.02 * kMpcSampleRate))   // a hit right at the start is the start
            markers.push_back (h.position);
    std::sort (markers.begin(), markers.end());
    return markers;
}

std::vector<int> gridMarkers (int length, int slices)
{
    std::vector<int> markers;
    for (int i = 1; i < slices; ++i)
        markers.push_back ((int) ((juce::int64) length * i / slices));
    return markers;
}

std::vector<int> snapToZeroCrossings (const juce::AudioBuffer<float>& b, std::vector<int> markers)
{
    const int reach = (int) (0.005 * kMpcSampleRate);
    const auto* d = b.getReadPointer (0);
    const int n = b.getNumSamples();

    for (auto& m : markers)
    {
        for (int offset = 0; offset < reach; ++offset)
        {
            for (int candidate : { m - offset, m + offset })
                if (candidate > 0 && candidate < n && (d[candidate - 1] <= 0.0f) != (d[candidate] <= 0.0f))
                {
                    m = candidate;
                    offset = reach;
                    break;
                }
        }
    }

    return markers;
}

std::vector<juce::AudioBuffer<float>> slice (const juce::AudioBuffer<float>& b, const std::vector<int>& markers)
{
    std::vector<int> edges { 0 };
    for (int m : markers)
        if (m > edges.back() && m < b.getNumSamples())
            edges.push_back (m);
    edges.push_back (b.getNumSamples());

    std::vector<juce::AudioBuffer<float>> slices;
    for (size_t i = 0; i + 1 < edges.size(); ++i)
    {
        auto s = range (b, edges[i], edges[i + 1]);
        // tiny fades so slices start and end cleanly
        const int fadeLength = juce::jmin (64, s.getNumSamples() / 4);
        fade (s, s.getNumSamples() - fadeLength, s.getNumSamples(), false);
        slices.push_back (std::move (s));
    }
    return slices;
}

double guessBeats (int lengthSamples, double bpm)
{
    if (lengthSamples <= 0 || bpm <= 0)
        return 0;

    const double beatsAtTempo = lengthSamples / kMpcSampleRate * bpm / 60.0;
    // Drum hits and other one-shots are left alone; stretching a kick to a beat makes no sense.
    if (beatsAtTempo < 0.9)
        return 0;

    double best = 0.25, bestError = 1.0e9;
    for (double beats = 0.25; beats <= 64.0; beats *= 2.0)
    {
        const double error = std::abs (std::log2 (beatsAtTempo / beats));
        if (error < bestError)
        {
            best = beats;
            bestError = error;
        }
    }
    return best;
}

double tempoOf (int lengthSamples, double beats)
{
    return lengthSamples > 0 && beats > 0 ? beats * 60.0 * kMpcSampleRate / lengthSamples : 0.0;
}

SharedAudio fitToTempo (const SharedAudio& audio, double beats, double bpm)
{
    if (audio == nullptr || beats <= 0 || bpm <= 0)
        return audio;

    const int target = (int) std::round (beats * 60.0 / bpm * kMpcSampleRate);
    if (std::abs (target - audio->getNumSamples()) < 2)
        return audio;

    return std::make_shared<juce::AudioBuffer<float>> (stretch (*audio, target));
}

} // namespace SoundFx
