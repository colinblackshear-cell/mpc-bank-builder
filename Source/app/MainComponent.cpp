#include "MainComponent.h"

#include "SampleTools.h"
#include "Theme.h"

namespace fs = std::filesystem;

MainComponent::MainComponent()
{
    formats.registerBasicFormats();

    appTitle.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    appTitle.setColour (juce::Label::textColourId, Theme::accent);
    memory.setJustificationType (juce::Justification::centredRight);
    status.setColour (juce::Label::textColourId, Theme::textDim);

    for (auto* c : std::initializer_list<juce::Component*> { &appTitle, &kitLabel, &kitName, &memory, &ram, &openButton,
                                                             &audioButton, &exportButton, &kitList, &newKit, &removeKit,
                                                             &grid, &editor, &status })
        addAndMakeVisible (c);

    kitName.setInputRestrictions (16);
    kitName.onTextChange = [this]
    {
        project.currentBank().name = kitName.getText();
        project.changed();
    };

    ram.addItem ("MPC has 2 MB", 2);
    ram.addItem ("MPC has 8 MB", 8);
    ram.addItem ("MPC has 16 MB", 16);
    ram.addItem ("MPC has 32 MB", 32);
    ram.setSelectedId (project.ramMegabytes, juce::dontSendNotification);
    ram.onChange = [this] { project.ramMegabytes = ram.getSelectedId(); project.changed(); };

    openButton.onClick = [this] { openKit(); };
    audioButton.onClick = [this] { showAudioSettings(); };
    exportButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    exportButton.setColour (juce::TextButton::textColourOffId, Theme::background);
    exportButton.onClick = [this] { exportToCard(); };

    kitList.setRowHeight (44);
    kitList.setColour (juce::ListBox::backgroundColourId, Theme::panel);
    newKit.onClick = [this] { project.addBank(); grid.setPadBank (0); selectPad (0); };
    removeKit.onClick = [this]
    {
        const auto& bank = project.currentBank();
        if (bank.usedPads() == 0)
        {
            project.removeBank (project.getCurrentIndex());
            return;
        }

        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon,
                                                                    "Remove " + bank.name + "?",
                                                                    "Its " + juce::String (bank.usedPads())
                                                                        + " pads will be cleared. Kits already exported to the card stay there.",
                                                                    "Remove", "Cancel", this);
        juce::AlertWindow::showAsync (options, [this] (int result)
        {
            if (result == 1)
                project.removeBank (project.getCurrentIndex());
        });
    };

    for (int i = 0; i < 4; ++i)
    {
        auto& tab = bankTabs[(size_t) i];
        tab.setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + i)));
        tab.setClickingTogglesState (true);
        tab.setRadioGroupId (1);
        tab.setColour (juce::TextButton::buttonOnColourId, Theme::accent);
        tab.setColour (juce::TextButton::textColourOnId, Theme::background);
        tab.onClick = [this, i] { grid.setPadBank (i); selectPad (i * kPadsPerBank); };
        addAndMakeVisible (tab);
    }
    bankTabs[0].setToggleState (true, juce::dontSendNotification);

    grid.onPadPressed = [this] (int pad)
    {
        selectPad (pad);
        engine.play (pad, project.currentBank().pads[(size_t) pad]);
    };
    grid.onPadReleased = [this] (int pad)
    {
        if (project.currentBank().pads[(size_t) pad].mode == PadMode::noteOff)
            engine.release (pad);
    };
    grid.onFilesDropped = [this] (int pad, const juce::StringArray& paths)
    {
        loadFilesIntoPads (pad, SampleTools::expandDroppedFiles (paths, formats));
    };
    grid.onClearPad = [this] (int pad)
    {
        project.currentBank().pads[(size_t) pad] = {};
        project.changed();
    };

    editor.onClear = [this] { grid.onClearPad (grid.getSelectedPad()); };
    editor.onLoadFile = [this] { chooseFileForPad(); };

    engine.onPadNoteOn = [this] (int pad, float velocity)
    {
        engine.play (pad, project.currentBank().pads[(size_t) pad], velocity);
        if (pad / kPadsPerBank == grid.getPadBank())
            grid.flash (pad);
    };
    engine.onPadNoteOff = [this] (int pad)
    {
        if (project.currentBank().pads[(size_t) pad].mode == PadMode::noteOff)
            engine.release (pad);
    };

    project.addChangeListener (this);

    setStatus ("Drop samples or a folder of samples on the pads. Click a pad or play your MIDI keyboard to hear it.");
    setSize (1180, 760);
    refresh();
}

MainComponent::~MainComponent()
{
    project.removeChangeListener (this);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (16);

    auto header = r.removeFromTop (36);
    appTitle.setBounds (header.removeFromLeft (220));
    exportButton.setBounds (header.removeFromRight (150));
    header.removeFromRight (8);
    audioButton.setBounds (header.removeFromRight (120));
    header.removeFromRight (8);
    openButton.setBounds (header.removeFromRight (110));
    header.removeFromRight (16);
    ram.setBounds (header.removeFromRight (140));
    header.removeFromRight (8);
    memory.setBounds (header.removeFromRight (200));

    r.removeFromTop (12);
    status.setBounds (r.removeFromBottom (24));
    r.removeFromBottom (8);

    auto left = r.removeFromLeft (200);
    auto leftButtons = left.removeFromBottom (30);
    newKit.setBounds (leftButtons.removeFromLeft (leftButtons.getWidth() / 2 - 4));
    leftButtons.removeFromLeft (8);
    removeKit.setBounds (leftButtons);
    left.removeFromBottom (8);
    kitList.setBounds (left);

    r.removeFromLeft (16);
    editor.setBounds (r.removeFromRight (340));
    r.removeFromRight (16);

    auto top = r.removeFromTop (30);
    kitLabel.setBounds (top.removeFromLeft (70));
    kitName.setBounds (top.removeFromLeft (220));
    top.removeFromLeft (16);
    for (auto& tab : bankTabs)
    {
        tab.setBounds (top.removeFromLeft (40));
        top.removeFromLeft (4);
    }

    r.removeFromTop (12);
    grid.setBounds (r);
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void MainComponent::refresh()
{
    auto& bank = project.currentBank();
    kitName.setText (bank.name, juce::dontSendNotification);

    const auto used = bank.memoryBytes();
    const auto available = (size_t) project.ramMegabytes * 1024 * 1024;
    memory.setText ("Kit uses " + SampleTools::formatMegabytes (used) + " of " + juce::String (project.ramMegabytes) + " MB",
                    juce::dontSendNotification);
    memory.setColour (juce::Label::textColourId, used > available ? Theme::warning : Theme::text);

    kitList.updateContent();
    kitList.selectRow (project.getCurrentIndex(), true, true);
    kitList.repaint();
    removeKit.setEnabled (project.getBanks().size() > 1 || bank.usedPads() > 0);

    editor.refresh();
    grid.repaint();
}

int MainComponent::getNumRows()
{
    return (int) project.getBanks().size();
}

void MainComponent::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= getNumRows())
        return;

    const auto& bank = project.getBanks()[(size_t) row];

    if (selected)
    {
        g.setColour (Theme::padFull);
        g.fillRect (0, 0, width, height);
    }

    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (bank.name, 10, 4, width - 20, 20, juce::Justification::centredLeft);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String (bank.usedPads()) + " pads, " + SampleTools::formatMegabytes (bank.memoryBytes()),
                10, 22, width - 20, 18, juce::Justification::centredLeft);
}

void MainComponent::selectedRowsChanged (int row)
{
    if (row >= 0 && row != project.getCurrentIndex())
    {
        engine.stopAll();
        project.setCurrent (row);
    }
}

void MainComponent::selectPad (int pad)
{
    grid.setSelectedPad (pad);
    editor.setPad (pad);
}

void MainComponent::loadFilesIntoPads (int firstPad, const juce::Array<juce::File>& files)
{
    if (files.isEmpty())
    {
        setStatus ("Those aren't audio files the app can read (WAV, AIFF, MP3, FLAC or .SND).", true);
        return;
    }

    juce::MouseCursor::showWaitCursor();

    auto& bank = project.currentBank();
    juce::StringArray problems;
    int pad = firstPad;
    int loaded = 0;

    for (const auto& file : files)
    {
        if (pad >= mpc::kPadCount)
        {
            problems.add (juce::String (files.size() - loaded) + " files didn't fit; the kit has 64 pads");
            break;
        }

        juce::String error;
        auto audio = SampleTools::load (file, formats, error);

        if (audio == nullptr)
        {
            problems.add (error);
            continue;
        }

        auto& p = bank.pads[(size_t) pad];
        p = {};
        p.audio = audio;
        p.name = file.getFileNameWithoutExtension().substring (0, 16);
        ++pad;
        ++loaded;
    }

    juce::MouseCursor::hideWaitCursor();

    bankTabs[(size_t) (firstPad / kPadsPerBank)].setToggleState (true, juce::dontSendNotification);
    grid.setPadBank (firstPad / kPadsPerBank);
    selectPad (firstPad);
    project.changed();

    if (problems.isEmpty())
        setStatus ("Loaded " + juce::String (loaded) + (loaded == 1 ? " sound" : " sounds") + " from pad " + padLabel (firstPad) + ".");
    else
        setStatus ("Loaded " + juce::String (loaded) + ". " + problems.joinIntoString (". "), true);
}

void MainComponent::chooseFileForPad()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sound onto pad " + padLabel (grid.getSelectedPad()),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   SampleTools::audioFileWildcard (formats));

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (! fc.getResults().isEmpty())
                                  loadFilesIntoPads (grid.getSelectedPad(), fc.getResults());
                          });
}

void MainComponent::openKit()
{
    chooser = std::make_unique<juce::FileChooser> ("Open an MPC program (.PGM)", juce::File ("/Volumes"), "*.pgm;*.PGM");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult() != juce::File())
                                  openKitFile (fc.getResult());
                          });
}

void MainComponent::openKitFile (const juce::File& file)
{
    juce::StringArray missing;
    juce::String error;
    auto bank = SampleTools::importProgram (file, missing, error);

    if (! bank)
    {
        setStatus (error, true);
        return;
    }

    // Opening into the untouched starting kit replaces it rather than adding a second one.
    const bool replaceFirst = project.getBanks().size() == 1 && project.currentBank().usedPads() == 0;
    const int index = replaceFirst ? 0 : project.addBank (bank->name);
    project.getBanks()[(size_t) index] = std::move (*bank);
    project.setCurrent (index);
    grid.setPadBank (0);
    bankTabs[0].setToggleState (true, juce::dontSendNotification);
    selectPad (0);
    project.changed();

    if (missing.isEmpty())
        setStatus ("Opened " + file.getFileName() + ".");
    else
        setStatus ("Opened " + file.getFileName() + ", but these sounds weren't next to it: "
                       + missing.joinIntoString (", "), true);
}

void MainComponent::exportToCard()
{
    int used = 0;
    for (const auto& bank : project.getBanks())
        used += bank.usedPads();

    if (used == 0)
    {
        setStatus ("Nothing to export yet. Drop some samples on the pads first.", true);
        return;
    }

    auto start = juce::File ("/Volumes");
    chooser = std::make_unique<juce::FileChooser> ("Choose your MPC's SD card (or any folder)",
                                                   start.isDirectory() ? start : juce::File::getSpecialLocation (juce::File::userDesktopDirectory));

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto dir = fc.getResult();
                              if (dir == juce::File() || ! dir.isDirectory())
                                  return;

                              // Warn before replacing kit folders that are already on the card.
                              std::vector<std::string> taken;
                              juce::StringArray existing;
                              for (const auto& bank : project.getBanks())
                              {
                                  if (bank.usedPads() == 0)
                                      continue;
                                  const auto folder = mpc::makeUniqueName (bank.name.toStdString(), taken);
                                  const auto onCard = dir.getChildFile (folder);
                                  if (onCard.isDirectory() && ! onCard.findChildFiles (juce::File::findFiles, false, "*.SND;*.snd;*.PGM;*.pgm").isEmpty())
                                      existing.add (folder);
                              }

                              if (existing.isEmpty())
                              {
                                  writeBanks (dir);
                                  return;
                              }

                              auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
                                  juce::MessageBoxIconType::WarningIcon, "Replace kits on the card?",
                                  "These folders already have sounds in them: " + existing.joinIntoString (", ")
                                      + ".\n\nTheir .SND and .PGM files will be replaced with this export.",
                                  "Replace", "Cancel", this);

                              juce::AlertWindow::showAsync (options, [this, dir] (int result)
                              {
                                  if (result == 1)
                                      writeBanks (dir);
                              });
                          });
}

void MainComponent::writeBanks (const juce::File& destination)
{
    juce::MouseCursor::showWaitCursor();

    const fs::path root (destination.getFullPathName().toStdString());
    const auto available = (size_t) project.ramMegabytes * 1024 * 1024;
    std::vector<std::string> taken;
    juce::StringArray written, tooBig, errors;

    for (const auto& bank : project.getBanks())
    {
        if (bank.usedPads() == 0)
            continue;

        auto files = mpc::buildBank (SampleTools::toMpcBank (bank), taken);
        auto result = mpc::writeBankFolder (root, files);

        if (! result.ok)
        {
            errors.add (juce::String (result.error));
            continue;
        }

        written.add (juce::String (files.folderName) + "/" + juce::String (files.folderName) + ".PGM ("
                     + juce::String ((int) files.sounds.size()) + " sounds, " + SampleTools::formatMegabytes (result.memoryBytes) + ")");

        if (result.memoryBytes > available)
            tooBig.add (juce::String (files.folderName));
    }

    // Finder leaves "._" files on cards; the MPC shows them as junk.
    mpc::removeMacMetadataFiles (root);

    juce::MouseCursor::hideWaitCursor();

    juce::String message;
    if (! written.isEmpty())
        message << "Exported to " << destination.getFullPathName() << ":\n\n" << written.joinIntoString ("\n")
                << "\n\nEject the card, put it in the MPC, then LOAD the .PGM inside each folder.";
    if (! tooBig.isEmpty())
        message << "\n\nToo big for " << project.ramMegabytes << " MB of MPC memory: " << tooBig.joinIntoString (", ")
                << ". Try saving pads as mono or trimming them.";
    if (! errors.isEmpty())
        message << "\n\nProblems:\n" << errors.joinIntoString ("\n");

    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (errors.isEmpty() ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (errors.isEmpty() ? "Export done" : "Export had problems")
                                      .withMessage (message)
                                      .withButton ("OK")
                                      .withAssociatedComponent (this),
                                  nullptr);

    setStatus (errors.isEmpty() ? "Exported " + juce::String (written.size()) + " kits to " + destination.getFileName() + "."
                                : errors[0],
               ! errors.isEmpty());
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager(), 0, 2, 0, 2, true, false, true, false);
    selector->setSize (520, 420);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle = "Audio and MIDI settings";
    options.dialogBackgroundColour = Theme::panel;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::setStatus (const juce::String& text, bool warning)
{
    status.setText (text, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, warning ? Theme::warning : Theme::textDim);
}
