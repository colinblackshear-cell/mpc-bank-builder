#include <juce_gui_basics/juce_gui_basics.h>

#include "MainComponent.h"
#include "Theme.h"

class MpcBankBuilderApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        auto& lf = juce::LookAndFeel::getDefaultLookAndFeel();
        lf.setColour (juce::ResizableWindow::backgroundColourId, Theme::background);
        lf.setColour (juce::Label::textColourId, Theme::text);
        lf.setColour (juce::TextButton::buttonColourId, Theme::padEmpty);
        lf.setColour (juce::ComboBox::backgroundColourId, Theme::padEmpty);
        lf.setColour (juce::TextEditor::backgroundColourId, Theme::padEmpty);
        lf.setColour (juce::Slider::thumbColourId, Theme::accent);
        lf.setColour (juce::Slider::trackColourId, Theme::padFull);

        mainWindow = std::make_unique<MainWindow> (getApplicationName());
        anotherInstanceStarted (commandLine);
    }

    // Files opened with the app (or passed on the command line).
    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (mainWindow == nullptr)
            return;

        for (const auto& arg : juce::StringArray::fromTokens (commandLine, true))
        {
            const juce::File file (arg.unquoted());
            auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent());
            if (main == nullptr || ! file.existsAsFile())
                continue;

            // A .PGM opens as a kit; any other sound opens in the sampler.
            if (file.hasFileExtension ("pgm"))
                main->openKitFile (file);
            else
                main->openAudioFile (file);
        }
    }

    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, Theme::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (980, 640, 4000, 3000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (MpcBankBuilderApplication)
