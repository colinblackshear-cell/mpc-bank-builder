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

    // `audio` is what the pad plays (Bank::audioToPlay); the pad gives volume, pan, tune and mode.
    void play (int pad, const Pad&, const SharedAudio& audio, float velocity = 1.0f);
    void release (int pad);
    void stopAll();

    // Called on the message thread when a MIDI note hits a pad; the app plays it from the
    // current bank, so the engine doesn't need to know about banks.
    std::function<void (int pad, float velocity)> onPadNoteOn;
    std::function<void (int pad)> onPadNoteOff;

    //==========================================================================
    // Sampler preview: plays part of a sound, and reports where it is.
    void preview (const SharedAudio&, int start, int end);
    void stopPreview();
    int getPreviewPosition() const { return previewPosition.load(); }   // -1 when not playing

    //==========================================================================
    // Recording from the audio input. The input opens only when the sampler asks for it, so the
    // app doesn't ask for microphone access until you want to record.
    bool setInputEnabled (bool shouldBeEnabled, juce::String& error);
    bool isInputEnabled() const;
    void startRecording();
    // Ends the recording and returns it at 44.1 kHz (1 or 2 channels).
    juce::AudioBuffer<float> stopRecording();
    bool isRecording() const { return recording.load(); }
    double getRecordedSeconds() const;
    float takeInputLevel() { return inputLevel.exchange (0.0f); }   // peak since the last call
    // Moves recorded audio off the audio thread; call it often (e.g. from a 30 Hz timer).
    void collectRecording();

    static constexpr int kPreviewPad = -100;

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
        int end = -1;               // stop here instead of at the end (preview)
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

    std::atomic<int> previewPosition { -1 };

    // Recording: the audio thread writes into a ring buffer; collectRecording() empties it.
    std::atomic<bool> recording { false };
    std::atomic<float> inputLevel { 0.0f };
    juce::AbstractFifo fifo { 1 << 18 };
    juce::AudioBuffer<float> ring { 2, 1 << 18 };
    std::atomic<int> recordChannels { 1 };
    std::array<std::vector<float>, 2> recorded;
    double recordRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
