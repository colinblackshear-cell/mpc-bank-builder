#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BankModel.h"

// The 4x4 pads of one MPC bank (A-D), laid out like the MPC: pad 13 top left, pad 1 bottom left.
// Drop audio files or a folder on a pad to fill it and the pads after it.
class PadGrid : public juce::Component,
                public juce::FileDragAndDropTarget,
                public juce::DragAndDropTarget
{
public:
    explicit PadGrid (Project&);

    void setPadBank (int bankIndex);   // 0..3 = A..D
    int getPadBank() const { return padBank; }

    void setSelectedPad (int pad);
    int getSelectedPad() const { return selected; }

    // Lights a pad briefly, e.g. when a MIDI note plays it.
    void flash (int pad);

    std::function<void (int pad)> onPadPressed;
    std::function<void (int pad)> onPadReleased;
    std::function<void (int pad, const juce::StringArray& files)> onFilesDropped;
    std::function<void (int pad)> onClearPad;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int x, int y) override { fileDragMove ({}, x, y); }
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // Sounds dragged from the library list.
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails& d) override { fileDragMove ({}, d.localPosition.x, d.localPosition.y); }
    void itemDragExit (const SourceDetails&) override { fileDragExit ({}); }
    void itemDropped (const SourceDetails&) override;

private:
    juce::Rectangle<int> boundsForSlot (int slot) const;   // slot 0..15 within the bank
    int padAt (juce::Point<int>) const;                    // -1 if none
    const std::vector<float>& peaksFor (const SharedAudio&);

    struct CachedPeaks
    {
        std::weak_ptr<const juce::AudioBuffer<float>> audio;
        std::vector<float> peaks;
    };
    std::map<const void*, CachedPeaks> peakCache;

    Project& project;
    int padBank = 0;
    int selected = 0;
    int dragHover = -1;
    int held = -1;
    std::array<juce::uint32, mpc::kPadCount> flashUntil {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadGrid)
};
