#include "SoundLibrary.h"

#include "Theme.h"

SoundLibrary::SoundLibrary (juce::AudioFormatManager& f, juce::File dir) : formats (f), folder (std::move (dir))
{
    folder.createDirectory();
    rescan();
}

juce::File SoundLibrary::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
        .getChildFile ("MPC Bank Builder")
        .getChildFile ("Sounds");
}

void SoundLibrary::rescan()
{
    entries.clear();

    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false))
    {
        if (file.getFileName().startsWith ("."))
            continue;

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->sampleRate <= 0)
            continue;

        Entry e;
        e.file = file;
        e.name = file.getFileNameWithoutExtension();
        e.seconds = (double) reader->lengthInSamples / reader->sampleRate;
        e.channels = (int) reader->numChannels;
        e.modified = file.getLastModificationTime();
        entries.push_back (e);
    }

    std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.modified > b.modified; });
    sendChangeMessage();
}

juce::File SoundLibrary::save (const juce::AudioBuffer<float>& audio, const juce::String& name)
{
    auto safe = juce::File::createLegalFileName (name.trim()).substring (0, 60);
    if (safe.isEmpty())
        safe = "Sound";

    auto file = folder.getNonexistentChildFile (safe, ".wav", false);

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    if (stream == nullptr)
        return {};

    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (kMpcSampleRate)
                                                   .withNumChannels (audio.getNumChannels())
                                                   .withBitsPerSample (24));
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
        return {};

    writer.reset();
    rescan();
    return file;
}

bool SoundLibrary::rename (const Entry& e, const juce::String& newName)
{
    auto safe = juce::File::createLegalFileName (newName.trim());
    if (safe.isEmpty())
        return false;

    auto target = folder.getNonexistentChildFile (safe, e.file.getFileExtension(), false);
    const bool ok = e.file.moveFileTo (target);
    rescan();
    return ok;
}

bool SoundLibrary::remove (const Entry& e)
{
    const bool ok = e.file.moveToTrash();
    rescan();
    return ok;
}

//==============================================================================
LibraryList::LibraryList (SoundLibrary& lib) : library (lib)
{
    title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    search.setTextToShowWhenEmpty ("Search sounds", Theme::textDim);
    search.onTextChange = [this] { filter(); };
    list.setRowHeight (40);
    list.setColour (juce::ListBox::backgroundColourId, Theme::panel);
    folderButton.onClick = [this] { library.getFolder().revealToUser(); };

    for (auto* c : std::initializer_list<juce::Component*> { &title, &search, &list, &folderButton })
        addAndMakeVisible (c);

    library.addChangeListener (this);
    filter();
}

LibraryList::~LibraryList()
{
    library.removeChangeListener (this);
}

void LibraryList::filter()
{
    shown.clear();
    const auto query = search.getText().trim();
    for (const auto& e : library.getEntries())
        if (query.isEmpty() || e.name.containsIgnoreCase (query))
            shown.push_back (e);
    list.updateContent();
    list.repaint();
}

void LibraryList::paint (juce::Graphics&) {}

void LibraryList::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (24);
    folderButton.setBounds (top.removeFromRight (90).reduced (0, 1));
    title.setBounds (top);
    r.removeFromTop (6);
    search.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);
    list.setBounds (r);
}

void LibraryList::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) shown.size())
        return;

    const auto& e = shown[(size_t) row];
    if (selected)
    {
        g.setColour (Theme::padFull);
        g.fillRect (0, 0, width, height);
    }

    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (e.name, 10, 3, width - 20, 18, juce::Justification::centredLeft);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String (e.seconds, 2) + " s, " + (e.channels > 1 ? "stereo" : "mono"),
                10, 21, width - 20, 15, juce::Justification::centredLeft);
}

void LibraryList::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) shown.size())
        return;

    if (e.mods.isPopupMenu())
        showMenu (row);
    else if (onOpen)
        onOpen (shown[(size_t) row]);
}

juce::var LibraryList::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.isEmpty() || rows[0] >= (int) shown.size())
        return {};
    return juce::String (SoundLibrary::dragPrefix) + shown[(size_t) rows[0]].file.getFullPathName();
}

void LibraryList::showMenu (int row)
{
    const auto entry = shown[(size_t) row];
    juce::PopupMenu menu;

    menu.addItem ("Rename...", [this, entry]
    {
        auto* window = new juce::AlertWindow ("Rename sound", {}, juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", entry.name);
        window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, entry, window] (int result)
        {
            if (result == 1)
                library.rename (entry, window->getTextEditorContents ("name"));
        }), true);
    });
    menu.addItem ("Show in Finder", [entry] { entry.file.revealToUser(); });
    menu.addSeparator();
    menu.addItem ("Move to Trash", [this, entry] { library.remove (entry); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}
