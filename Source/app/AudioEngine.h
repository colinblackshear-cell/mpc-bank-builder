#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "BankModel.h"

// Plays pads for auditioning, from mouse clicks or a MIDI keyboard. MIDI notes map to pads the
// same way the MPC maps them, so a keyboard plays the same pads here as on the MPC.
class AudioEngine : private juce::AudioIODeviceCallback,
                    private juce::MidiInputCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

    void play (int pad, const Pad&, float velocity = 1.0f);
    void release (int pad);
    void stopAll();

    // Called on the message thread when a MIDI note hits a pad; the app plays it from the
    // current bank, so the engine doesn't need to know about banks.
    std::function<void (int pad, float velocity)> onPadNoteOn;
    std::function<void (int pad)> onPadNoteOff;

private:
    struct Voice
    {
        SharedAudio audio;
        int pad = 0;
        double position = 0;
        double step = 1;
        float gainLeft = 1, gainRight = 1;
        bool reverse = false;
        bool releasing = false;
        float fade = 1;
    };

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    juce::AudioDeviceManager deviceManager;
    juce::SpinLock lock;
    std::vector<Voice> voices;
    double deviceRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
