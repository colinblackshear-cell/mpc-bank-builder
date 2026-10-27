#include "AudioEngine.h"

namespace
{
    constexpr size_t kMaxVoices = 32;
    constexpr float kReleaseStep = 1.0f / 256.0f;   // about 6 ms fade when a pad is let go
}

AudioEngine::AudioEngine()
{
    voices.reserve (kMaxVoices);

    // Output only for now; sampling (milestone 3) will open an input too.
    deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (this);
    deviceManager.addMidiInputDeviceCallback ({}, this);   // every enabled MIDI input

    // Turn on every connected keyboard so it just works.
    for (const auto& input : juce::MidiInput::getAvailableDevices())
        deviceManager.setMidiInputDeviceEnabled (input.identifier, true);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeAudioCallback (this);
}

void AudioEngine::play (int pad, const Pad& p, float velocity)
{
    if (p.isEmpty())
        return;

    Voice v;
    v.audio = p.audio;
    v.pad = pad;
    v.reverse = p.reverse;
    v.position = p.reverse ? p.audio->getNumSamples() - 1 : 0;
    v.step = (kMpcSampleRate / deviceRate) * std::pow (2.0, p.tune / 120.0);

    float gain = juce::jlimit (0.0f, 1.0f, velocity) * (float) p.volume / 100.0f;

    if (p.normalize)
    {
        const auto peak = p.audio->getMagnitude (0, p.audio->getNumSamples());
        if (peak > 0.0001f)
            gain *= 0.98f / peak;
    }

    const float pan = (float) p.pan / 100.0f;
    v.gainLeft = gain * std::cos (pan * juce::MathConstants<float>::halfPi) * juce::MathConstants<float>::sqrt2;
    v.gainRight = gain * std::sin (pan * juce::MathConstants<float>::halfPi) * juce::MathConstants<float>::sqrt2;

    const juce::SpinLock::ScopedLockType sl (lock);

    // Mono pads cut off their own earlier hits, like on the MPC.
    if (p.mode == PadMode::mono || p.mode == PadMode::noteOff)
        for (auto& existing : voices)
            if (existing.pad == pad)
                existing.releasing = true;

    if (voices.size() >= kMaxVoices)
        voices.erase (voices.begin());

    voices.push_back (std::move (v));
}

void AudioEngine::release (int pad)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    for (auto& v : voices)
        if (v.pad == pad)
            v.releasing = true;
}

void AudioEngine::stopAll()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    for (auto& v : voices)
        v.releasing = true;
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    deviceRate = device->getCurrentSampleRate() > 0 ? device->getCurrentSampleRate() : 44100.0;
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                     float* const* outputs, int numOutputs, int numSamples,
                                                     const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    if (numOutputs == 0)
        return;

    float* left = outputs[0];
    float* right = numOutputs > 1 ? outputs[1] : nullptr;

    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked())
        return;

    for (auto& v : voices)
    {
        const auto& audio = *v.audio;
        const int length = audio.getNumSamples();
        const float* l = audio.getReadPointer (0);
        const float* r = audio.getReadPointer (audio.getNumChannels() > 1 ? 1 : 0);

        for (int i = 0; i < numSamples; ++i)
        {
            const int index = (int) v.position;
            if (index < 0 || index >= length || v.fade <= 0.0f)
            {
                v.fade = 0.0f;
                break;
            }

            const float frac = (float) (v.position - index);
            const int next = juce::jlimit (0, length - 1, index + (v.reverse ? -1 : 1));
            const float sl0 = l[index] + (l[next] - l[index]) * frac;
            const float sr0 = r[index] + (r[next] - r[index]) * frac;

            if (right != nullptr)
            {
                left[i] += sl0 * v.gainLeft * v.fade;
                right[i] += sr0 * v.gainRight * v.fade;
            }
            else
            {
                left[i] += 0.5f * (sl0 * v.gainLeft + sr0 * v.gainRight) * v.fade;
            }

            v.position += v.reverse ? -v.step : v.step;

            if (v.releasing)
                v.fade -= kReleaseStep;
        }
    }

    voices.erase (std::remove_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.fade <= 0.0f; }),
                  voices.end());
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    const int pad = padForNote (m.getNoteNumber());
    if (pad < 0 || ! (m.isNoteOn() || m.isNoteOff()))
        return;

    const bool on = m.isNoteOn();
    const float velocity = m.getFloatVelocity();

    juce::MessageManager::callAsync ([this, pad, on, velocity]
    {
        if (on && onPadNoteOn)
            onPadNoteOn (pad, velocity);
        else if (! on && onPadNoteOff)
            onPadNoteOff (pad);
    });
}
