#include "PadGrid.h"

#include "SoundLibrary.h"
#include "Theme.h"

PadGrid::PadGrid (Project& p) : project (p) {}

void PadGrid::setPadBank (int bankIndex)
{
    padBank = juce::jlimit (0, 3, bankIndex);
    repaint();
}

void PadGrid::setSelectedPad (int pad)
{
    selected = pad;
    repaint();
}

void PadGrid::flash (int pad)
{
    flashUntil[(size_t) pad] = juce::Time::getMillisecondCounter() + 150;
    repaint();
    juce::Timer::callAfterDelay (160, [safe = juce::Component::SafePointer<PadGrid> (this)]
    {
        if (safe != nullptr)
            safe->repaint();
    });
}

juce::Rectangle<int> PadGrid::boundsForSlot (int slot) const
{
    const int gap = 10;
    const int size = juce::jmin ((getWidth() - gap * 3) / 4, (getHeight() - gap * 3) / 4);
    const int originX = (getWidth() - (size * 4 + gap * 3)) / 2;
    const int originY = (getHeight() - (size * 4 + gap * 3)) / 2;

    const int column = slot % 4;
    const int row = 3 - slot / 4;   // pads 1-4 along the bottom
    return { originX + column * (size + gap), originY + row * (size + gap), size, size };
}

int PadGrid::padAt (juce::Point<int> pos) const
{
    for (int slot = 0; slot < kPadsPerBank; ++slot)
        if (boundsForSlot (slot).contains (pos))
            return padBank * kPadsPerBank + slot;
    return -1;
}

const std::vector<float>& PadGrid::peaksFor (const SharedAudio& audio)
{
    auto& cached = peakCache[audio.get()];

    if (cached.audio.lock() != audio || cached.peaks.empty())
    {
        constexpr int buckets = 200;
        const int n = audio->getNumSamples();
        cached.audio = audio;
        cached.peaks.assign (buckets, 0.0f);

        for (int b = 0; b < buckets; ++b)
        {
            const int from = (int) ((juce::int64) n * b / buckets);
            const int to = juce::jmax (from + 1, (int) ((juce::int64) n * (b + 1) / buckets));
            cached.peaks[(size_t) b] = audio->getMagnitude (from, juce::jmin (to, n) - from);
        }
    }

    // forget sounds that are gone
    for (auto it = peakCache.begin(); it != peakCache.end();)
        it = it->second.audio.expired() ? peakCache.erase (it) : std::next (it);

    return peakCache[audio.get()].peaks;
}

void PadGrid::paint (juce::Graphics& g)
{
    const auto& bank = project.currentBank();
    const auto now = juce::Time::getMillisecondCounter();

    for (int slot = 0; slot < kPadsPerBank; ++slot)
    {
        const int pad = padBank * kPadsPerBank + slot;
        const auto& p = bank.pads[(size_t) pad];
        auto r = boundsForSlot (slot).toFloat();

        const bool lit = flashUntil[(size_t) pad] > now || held == pad;
        auto fill = p.isEmpty() ? Theme::padEmpty : Theme::padFull;
        if (lit)
            fill = Theme::accent;
        else if (dragHover == pad)
            fill = fill.brighter (0.3f);

        g.setColour (fill);
        g.fillRoundedRectangle (r, 8.0f);

        if (pad == selected)
        {
            g.setColour (Theme::accent);
            g.drawRoundedRectangle (r.reduced (1.5f), 8.0f, 3.0f);
        }

        auto inner = r.reduced (8.0f);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (padLabel (pad), inner.removeFromTop (16), juce::Justification::topLeft);

        if (! p.isEmpty())
        {
            // a small waveform of the pad's sound
            auto wave = inner.removeFromBottom (inner.getHeight() * 0.45f);
            const auto& peaks = peaksFor (p.audio);
            const int columns = juce::jmax (1, (int) wave.getWidth());
            g.setColour (lit ? Theme::background : Theme::waveform);

            for (int x = 0; x < columns; ++x)
            {
                const auto bucket = (size_t) (x * (int) peaks.size() / columns);
                const int column = p.reverse ? columns - 1 - x : x;
                const float h = juce::jmax (1.0f, peaks[bucket] * wave.getHeight());
                g.fillRect (wave.getX() + (float) column, wave.getCentreY() - h / 2, 1.0f, h);
            }

            g.setColour (lit ? Theme::background : Theme::text);
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawFittedText (p.name, inner.toNearestInt(), juce::Justification::centredLeft, 2);
        }
        else if (dragHover == pad)
        {
            g.setColour (Theme::text);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("Drop here", inner, juce::Justification::centred);
        }
    }
}

void PadGrid::mouseDown (const juce::MouseEvent& e)
{
    const int pad = padAt (e.getPosition());
    if (pad < 0)
        return;

    if (e.mods.isPopupMenu())
    {
        setSelectedPad (pad);
        juce::PopupMenu menu;
        menu.addItem ("Clear " + padLabel (pad), ! project.currentBank().pads[(size_t) pad].isEmpty(),
                      false, [this, pad] { if (onClearPad) onClearPad (pad); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        return;
    }

    held = pad;
    setSelectedPad (pad);
    if (onPadPressed)
        onPadPressed (pad);
}

void PadGrid::mouseUp (const juce::MouseEvent&)
{
    if (held >= 0 && onPadReleased)
        onPadReleased (held);
    held = -1;
    repaint();
}

void PadGrid::fileDragMove (const juce::StringArray&, int x, int y)
{
    const int pad = padAt ({ x, y });
    if (pad != dragHover)
    {
        dragHover = pad;
        repaint();
    }
}

void PadGrid::fileDragExit (const juce::StringArray&)
{
    dragHover = -1;
    repaint();
}

void PadGrid::filesDropped (const juce::StringArray& files, int x, int y)
{
    dragHover = -1;
    int pad = padAt ({ x, y });

    // Dropped between pads: start at the first empty pad of this bank.
    if (pad < 0)
    {
        pad = padBank * kPadsPerBank;
        for (int slot = 0; slot < kPadsPerBank; ++slot)
            if (project.currentBank().pads[(size_t) (padBank * kPadsPerBank + slot)].isEmpty())
            {
                pad = padBank * kPadsPerBank + slot;
                break;
            }
    }

    if (onFilesDropped)
        onFilesDropped (pad, files);
    repaint();
}

bool PadGrid::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith (SoundLibrary::dragPrefix);
}

void PadGrid::itemDropped (const SourceDetails& d)
{
    const auto path = d.description.toString().fromFirstOccurrenceOf (SoundLibrary::dragPrefix, false, false);
    filesDropped ({ path }, d.localPosition.x, d.localPosition.y);
}
