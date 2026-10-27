#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BankModel.h"

// A zoomable waveform with a selection and chop markers.
//  - drag across the waveform to select
//  - drag a marker's handle (the strip along the top) to move it; double-click to add one;
//    right-click a handle to remove it
//  - scroll wheel or pinch to zoom, shift + wheel (or a sideways swipe) to scroll
class WaveEditor : public juce::Component
{
public:
    WaveEditor();

    void setAudio (SharedAudio, bool keepView = false);
    const SharedAudio& getAudio() const { return audio; }

    juce::Range<int> getSelection() const { return selection; }
    void setSelection (juce::Range<int>);
    bool hasSelection() const { return selection.getLength() > 0; }

    const std::vector<int>& getMarkers() const { return markers; }
    void setMarkers (std::vector<int>);

    void setPlayhead (int sample);   // -1 hides it

    std::function<void()> onChange;   // selection or markers changed

    void paint (juce::Graphics&) override;
    void resized() override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;

private:
    static constexpr int kHandleHeight = 22;

    float xForSample (double sample) const;
    int sampleForX (float x) const;
    int markerAt (juce::Point<float>) const;   // -1 if none
    void zoom (float factor, float aroundX);
    void clampView();
    void rebuildPeaks();

    SharedAudio audio;
    std::vector<float> peaksMin, peaksMax;   // per 256-sample block, channel 0 + 1 mixed
    static constexpr int kBlock = 256;

    double viewStart = 0, viewLength = 1;    // in samples
    juce::Range<int> selection;
    std::vector<int> markers;
    int playhead = -1;

    int dragMarker = -1;
    int dragAnchor = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveEditor)
};
