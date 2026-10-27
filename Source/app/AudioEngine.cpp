#include "AudioEngine.h"

#include "SampleTools.h"

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

void AudioEngine::play (int pad, const Pad& p, const SharedAudio& audio, float velocity)
{
    if (audio == nullptr || audio->getNumSamples() == 0)
        return;

    Voice v;
    v.audio = audio;
    v.pad = pad;
    v.reverse = p.reverse;
    v.position = p.reverse ? audio->getNumSamples() - 1 : 0;
    v.step = (kMpcSampleRate / deviceRate) * std::pow (2.0, p.tune / 120.0);

    float gain = juce::jlimit (0.0f, 1.0f, velocity) * (float) p.volume / 100.0f;

    if (p.normalize)
    {
        const auto peak = audio->getMagnitude (0, audio->getNumSamples());
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

void AudioEngine::preview (const SharedAudio& audio, int start, int end)
{
    stopPreview();
    if (audio == nullptr || audio->getNumSamples() == 0)
        return;

    Voice v;
    v.audio = audio;
    v.pad = kPreviewPad;
    v.position = juce::jlimit (0, audio->getNumSamples() - 1, start);
    v.end = juce::jlimit (start + 1, audio->getNumSamples(), end);
    v.step = kMpcSampleRate / deviceRate;

    const juce::SpinLock::ScopedLockType sl (lock);
    voices.push_back (std::move (v));
    previewPosition = start;
}

void AudioEngine::stopPreview()
{
    release (kPreviewPad);
}

bool AudioEngine::setInputEnabled (bool shouldBeEnabled, juce::String& error)
{
    if (shouldBeEnabled == isInputEnabled())
        return true;

    auto setup = deviceManager.getAudioDeviceSetup();

    // The app starts with no input device at all; pick the system's default one.
    if (shouldBeEnabled && setup.inputDeviceName.isEmpty())
        if (auto* type = deviceManager.getCurrentDeviceTypeObject())
        {
            type->scanForDevices();
            const auto names = type->getDeviceNames (true);
            const int index = type->getDefaultDeviceIndex (true);
            if (juce::isPositiveAndBelow (index, names.size()))
                setup.inputDeviceName = names[index];
        }

    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    if (shouldBeEnabled)
        setup.inputChannels.setRange (0, 2, true);

    error = deviceManager.setAudioDeviceSetup (setup, true);

    if (error.isEmpty() && shouldBeEnabled && ! isInputEnabled())
        error = "No audio input found. Plug in a microphone or pick an input in Audio/MIDI.";

    return error.isEmpty();
}

bool AudioEngine::isInputEnabled() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    return device != nullptr && device->getActiveInputChannels().countNumberOfSetBits() > 0;
}

void AudioEngine::startRecording()
{
    for (auto& r : recorded)
        r.clear();
    fifo.reset();
    recordRate = deviceRate;
    recording = true;
}

void AudioEngine::collectRecording()
{
    const int ready = fifo.getNumReady();
    if (ready == 0)
        return;

    const auto scope = fifo.read (ready);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& r = recorded[(size_t) ch];
        const auto* d = ring.getReadPointer (ch);
        r.insert (r.end(), d + scope.startIndex1, d + scope.startIndex1 + scope.blockSize1);
        r.insert (r.end(), d + scope.startIndex2, d + scope.startIndex2 + scope.blockSize2);
    }
}

double AudioEngine::getRecordedSeconds() const
{
    return (double) recorded[0].size() / recordRate;
}

juce::AudioBuffer<float> AudioEngine::stopRecording()
{
    recording = false;
    collectRecording();

    const int channels = juce::jlimit (1, 2, recordChannels.load());
    const int n = (int) recorded[0].size();
    juce::AudioBuffer<float> raw (channels, juce::jmax (1, n));
    raw.clear();
    for (int ch = 0; ch < channels; ++ch)
        if (n > 0)
            raw.copyFrom (ch, 0, recorded[(size_t) ch].data(), n);

    for (auto& r : recorded)
        r.clear();

    return *SampleTools::resample (raw, recordRate);
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

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                     float* const* outputs, int numOutputs, int numSamples,
                                                     const juce::AudioIODeviceCallbackContext&)
{
    // Input: meter it, and record it if we're recording.
    if (numInputs > 0 && inputs[0] != nullptr)
    {
        float peak = 0;
        for (int ch = 0; ch < juce::jmin (2, numInputs); ++ch)
            if (inputs[ch] != nullptr)
                peak = juce::jmax (peak, juce::FloatVectorOperations::findMaximum (inputs[ch], numSamples),
                                   -juce::FloatVectorOperations::findMinimum (inputs[ch], numSamples));
        if (peak > inputLevel.load())
            inputLevel = peak;

        if (recording.load())
        {
            // A stereo input that's really one mic on the left records as mono.
            const bool stereo = numInputs > 1 && inputs[1] != nullptr;
            recordChannels = stereo ? 2 : 1;
            const auto scope = fifo.write (juce::jmin (numSamples, fifo.getFreeSpace()));
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* src = inputs[stereo ? ch : 0];
                if (scope.blockSize1 > 0)
                    ring.copyFrom (ch, scope.startIndex1, src, scope.blockSize1);
                if (scope.blockSize2 > 0)
                    ring.copyFrom (ch, scope.startIndex2, src + scope.blockSize1, scope.blockSize2);
            }
        }
    }

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
            if (index < 0 || index >= (v.end >= 0 ? v.end : length) || v.fade <= 0.0f)
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

        if (v.pad == kPreviewPad)
            previewPosition = v.fade > 0.0f ? (int) v.position : -1;
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
