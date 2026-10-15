#include "Editor.h"

namespace tonedock {
class Application : public juce::JUCEApplication,private juce::Timer {
public:
    const juce::String getApplicationName() override{return "ToneDock";}
    const juce::String getApplicationVersion() override{return "0.1.0";}
    bool moreThanOneInstanceAllowed() override{return false;}
    void initialise(const juce::String& commandLine) override {
        processor=std::make_unique<Processor>();
        auto session=defaultDataDirectory().getChildFile("last-session.tonedock");juce::MemoryBlock saved;
        if(session.existsAsFile() && session.getSize()<=1024*1024 && session.loadFileAsData(saved))processor->setStateInformation(saved.getData(),int(saved.getSize()));
        window=std::make_unique<Window>(*this,*processor);
        processor->onAudioSettings=[this]{showAudioSettings();};
        auto tokens=juce::StringArray::fromTokens(commandLine,true);
        // A browser protocol invocation cannot enable developer CLI switches.
        if(!commandLine.containsIgnoreCase("tonedock://"))
            for(auto& token:tokens)if(token.startsWith("--screenshot="))screenshot=juce::File(token.fromFirstOccurrenceOf("=",false,false).unquoted());
        if(screenshot==juce::File() && !tokens.contains("--no-audio")) {
            auto settings=juce::parseXML(defaultDataDirectory().getChildFile("audio-device.xml"));
            auto error=devices.initialise(0,2,settings.get(),true);
            if(error.isNotEmpty())processor->setMessage("Audio device: "+error+". Choose Audio settings.");
            player.setProcessor(processor.get());devices.addAudioCallback(&player);
        }
        handleArguments(commandLine);
        if(pending.isNotEmpty()){handleArguments(pending);pending.clear();}
        if(screenshot!=juce::File())startTimer(1800);
    }
    void shutdown() override {
        stopTimer();settingsWindow.reset();devices.removeAudioCallback(&player);player.setProcessor(nullptr);
        if(processor){juce::MemoryBlock state;processor->getStateInformation(state);juce::TemporaryFile temporary(defaultDataDirectory().getChildFile("last-session.tonedock"));
            if(temporary.getFile().replaceWithData(state.getData(),state.getSize()))temporary.overwriteTargetFileWithTemporary();}
        if(auto settings=devices.createStateXml())settings->writeTo(defaultDataDirectory().getChildFile("audio-device.xml"));
        window.reset();processor.reset();
    }
    void anotherInstanceStarted(const juce::String& line) override {
        if(!window){pending=line;return;}window->toFront(true);handleArguments(line);
    }
    void systemRequestedQuit() override{quit();}
private:
    class Window : public juce::DocumentWindow {
    public:
        Window(Application& a,Processor& p):DocumentWindow("ToneDock",juce::Colour(0xff111715),allButtons),app(a) {
            setUsingNativeTitleBar(true);setContentOwned(p.createEditorIfNeeded(),true);setResizable(true,true);setResizeLimits(1120,810,1700,1150);centreWithSize(getWidth(),getHeight());setVisible(true);
        }
        void closeButtonPressed() override{app.systemRequestedQuit();}
    private:Application& app;
    };
    void handleArguments(const juce::String& commandLine) {
        juce::StringArray files;
        for(auto token:juce::StringArray::fromTokens(commandLine,true)) {
            token=token.unquoted();if(token.startsWith("--"))continue;
            if(parseToneLink(token))processor->openToneLink(token);
            else if(!commandLine.containsIgnoreCase("tonedock://") && juce::File::isAbsolutePath(token) && juce::File(token).exists())files.add(token);
        }
        if(!files.isEmpty())processor->importFiles(files);
    }
    void showAudioSettings() {
        juce::DialogWindow::LaunchOptions options;options.dialogTitle="Audio interface / sample rate / buffer";
        auto* selector=new juce::AudioDeviceSelectorComponent(devices,0,2,2,2,false,false,true,false);
        selector->setSize(600,480);options.content.setOwned(selector);options.componentToCentreAround=window.get();
        options.useNativeTitleBar=true;options.resizable=true;settingsWindow.reset(options.create());settingsWindow->setVisible(true);
    }
    void timerCallback() override {
        stopTimer();auto* editor=processor->getActiveEditor();
        if(editor){auto image=editor->createComponentSnapshot(editor->getLocalBounds());screenshot.getParentDirectory().createDirectory();
            if(auto stream=screenshot.createOutputStream()){stream->setPosition(0);stream->truncate();juce::PNGImageFormat().writeImageToStream(image,*stream);}}
        quit();
    }
    juce::AudioDeviceManager devices;juce::AudioProcessorPlayer player;
    std::unique_ptr<Processor> processor;std::unique_ptr<Window> window;std::unique_ptr<juce::DialogWindow> settingsWindow;
    juce::String pending;juce::File screenshot;
};
}
START_JUCE_APPLICATION(tonedock::Application)
