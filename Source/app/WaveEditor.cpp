#include "WaveEditor.h"

#include "Theme.h"

namespace
{
    const juce::Colour markerColour { 0xff4fc3f7 };
}

WaveEditor::WaveEditor()
{
    setWantsKeyboardFocus (false);
}

void WaveEditor::setAudio (SharedAudio a, bool keepView)
{
    audio = std::move (a);
    rebuildPeaks();

    const int n = audio != nullptr ? audio->getNumSamples() : 0;
    if (! keepView || viewLength <= 1)
    {
        viewStart = 0;
        viewLength = juce::jmax (1, n);
    }
    clampView();

    selection = selection.getIntersectionWith ({ 0, n });
    markers.erase (std::remove_if (markers.begin(), markers.end(), [n] (int m) { return m <= 0 || m >= n; }), markers.end());
    repaint();
}

void WaveEditor::setSelection (juce::Range<int> r)
{
    const int n = audio != nullptr ? audio->getNumSamples() : 0;
    selection = r.getIntersectionWith ({ 0, n });
    repaint();
}

void WaveEditor::setMarkers (std::vector<int> m)
{
    markers = std::move (m);
    std::sort (markers.begin(), markers.end());
    repaint();
}

void WaveEditor::setPlayhead (int sample)
{
    if (sample != playhead)
    {
        playhead = sample;
        repaint();
    }
}

void WaveEditor::rebuildPeaks()
{
    peaksMin.clear();
    peaksMax.clear();
    if (audio == nullptr)
        return;

    const int n = audio->getNumSamples();
    const int blocks = (n + kBlock - 1) / kBlock;
    peaksMin.assign ((size_t) blocks, 0.0f);
    peaksMax.assign ((size_t) blocks, 0.0f);

    for (int ch = 0; ch < audio->getNumChannels(); ++ch)
    {
        const auto* d = audio->getReadPointer (ch);
        for (int b = 0; b < blocks; ++b)
        {
            const int from = b * kBlock, to = juce::jmin (n, from + kBlock);
            auto range = juce::FloatVectorOperations::findMinAndMax (d + from, to - from);
            peaksMin[(size_t) b] = juce::jmin (peaksMin[(size_t) b], range.getStart());
            peaksMax[(size_t) b] = juce::jmax (peaksMax[(size_t) b], range.getEnd());
        }
    }
}

float WaveEditor::xForSample (double sample) const
{
    return (float) ((sample - viewStart) / viewLength * getWidth());
}

int WaveEditor::sampleForX (float x) const
{
    const int n = audio != nullptr ? audio->getNumSamples() : 0;
    return juce::jlimit (0, n, (int) std::round (viewStart + x / juce::jmax (1, getWidth()) * viewLength));
}

void WaveEditor::clampView()
{
    const double n = audio != nullptr ? audio->getNumSamples() : 1;
    viewLength = juce::jlimit (juce::jmin (n, 64.0), juce::jmax (1.0, n), viewLength);
    viewStart = juce::jlimit (0.0, juce::jmax (0.0, n - viewLength), viewStart);
}

void WaveEditor::zoom (float factor, float aroundX)
{
    const double anchor = viewStart + aroundX / juce::jmax (1, getWidth()) * viewLength;
    viewLength /= factor;
    clampView();
    viewStart = anchor - aroundX / juce::jmax (1, getWidth()) * viewLength;
    clampView();
    repaint();
}

void WaveEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (bounds.toFloat(), 6.0f);

    auto handles = bounds.removeFromTop (kHandleHeight);
    auto wave = bounds.reduced (0, 6);

    if (audio == nullptr || peaksMax.empty())
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (16.0f));
        g.drawText ("Record something, or pick a sound from the list", wave, juce::Justification::centred);
        return;
    }

    // selection
    if (hasSelection())
    {
        const float x1 = xForSample (selection.getStart()), x2 = xForSample (selection.getEnd());
        g.setColour (Theme::accent.withAlpha (0.22f));
        g.fillRect (juce::Rectangle<float> (x1, (float) wave.getY(), x2 - x1, (float) wave.getHeight()));
    }

    // waveform
    const float mid = (float) wave.getCentreY();
    const float half = (float) wave.getHeight() * 0.5f;
    g.setColour (Theme::waveform);

    for (int x = 0; x < getWidth(); ++x)
    {
        const int from = (int) (viewStart + (double) x / getWidth() * viewLength) / kBlock;
        const int to = juce::jmax (from + 1, (int) (viewStart + (double) (x + 1) / getWidth() * viewLength) / kBlock);
        float lo = 0, hi = 0;
        for (int b = from; b < to && b < (int) peaksMax.size(); ++b)
        {
            lo = juce::jmin (lo, peaksMin[(size_t) b]);
            hi = juce::jmax (hi, peaksMax[(size_t) b]);
        }
        g.fillRect ((float) x, mid - hi * half, 1.0f, juce::jmax (1.0f, (hi - lo) * half));
    }

    // markers, numbered by the slice they start
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const float x = xForSample (markers[i]);
        if (x < -20 || x > getWidth() + 20)
            continue;
        g.setColour (markerColour);
        g.drawLine (x, (float) handles.getY(), x, (float) wave.getBottom(), 1.5f);
        auto handle = juce::Rectangle<float> (x, (float) handles.getY() + 2, 22.0f, (float) kHandleHeight - 4);
        g.fillRoundedRectangle (handle, 3.0f);
        g.setColour (Theme::background);
        g.drawText (juce::String ((int) i + 2), handle, juce::Justification::centred);
    }

    if (playhead >= 0)
    {
        g.setColour (Theme::text);
        const float x = xForSample (playhead);
        g.drawLine (x, (float) wave.getY(), x, (float) wave.getBottom(), 1.5f);
    }

    // a zoom indicator when not showing the whole sound
    const double n = audio->getNumSamples();
    if (viewLength < n)
    {
        auto bar = getLocalBounds().removeFromBottom (4).toFloat();
        g.setColour (Theme::padFull);
        g.fillRect (bar);
        g.setColour (Theme::textDim);
        g.fillRect (bar.withX ((float) (viewStart / n) * bar.getWidth()).withWidth ((float) (viewLength / n) * bar.getWidth()));
    }
}

int WaveEditor::markerAt (juce::Point<float> p) const
{
    for (int i = (int) markers.size() - 1; i >= 0; --i)
    {
        const float x = xForSample (markers[(size_t) i]);
        if (p.y < kHandleHeight + 4 ? (p.x >= x - 4 && p.x <= x + 24) : std::abs (p.x - x) < 5)
            return i;
    }
    return -1;
}

void WaveEditor::mouseDown (const juce::MouseEvent& e)
{
    if (audio == nullptr)
        return;

    dragMarker = markerAt (e.position);

    if (dragMarker >= 0 && e.mods.isPopupMenu())
    {
        markers.erase (markers.begin() + dragMarker);
        dragMarker = -1;
        repaint();
        if (onChange)
            onChange();
        return;
    }

    if (dragMarker < 0)
    {
        dragAnchor = sampleForX (e.position.x);
        selection = { dragAnchor, dragAnchor };
        repaint();
    }
}

void WaveEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (audio == nullptr)
        return;

    const int s = sampleForX (e.position.x);

    if (dragMarker >= 0)
    {
        markers[(size_t) dragMarker] = juce::jlimit (1, audio->getNumSamples() - 1, s);
    }
    else
    {
        selection = { juce::jmin (dragAnchor, s), juce::jmax (dragAnchor, s) };
    }
    repaint();
}

void WaveEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragMarker >= 0)
        std::sort (markers.begin(), markers.end());
    dragMarker = -1;
    if (onChange)
        onChange();
}

void WaveEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (audio == nullptr)
        return;

    const int s = sampleForX (e.position.x);
    if (s > 0 && s < audio->getNumSamples())
    {
        markers.push_back (s);
        std::sort (markers.begin(), markers.end());
        selection = {};
        repaint();
        if (onChange)
            onChange();
    }
}

void WaveEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (audio == nullptr)
        return;

    if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const float delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        viewStart -= delta * viewLength * 0.5;
        clampView();
        repaint();
    }
    else
    {
        zoom (juce::jlimit (0.5f, 2.0f, 1.0f + wheel.deltaY * 1.5f), e.position.x);
    }
}

void WaveEditor::mouseMagnify (const juce::MouseEvent& e, float scale)
{
    zoom (scale, e.position.x);
}
