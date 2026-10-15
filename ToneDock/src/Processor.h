#pragma once
#include "Engine.h"
#include "Library.h"
#include "Client.h"

namespace tonedock {
struct Services {
    Library library;
    t3k::Client tone3000;
    Services();
    bool configureKey(const juce::String&);
};
class Processor : public juce::AudioProcessor,private juce::Timer {
public:
    Processor();
    ~Processor() override;
    const juce::String getName() const override { return "ToneDock"; }
    void prepareToPlay(double,int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return parameters.getParameter("bypass"); }
    double getTailLengthSeconds() const override { return 8.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    void addEffect(const juce::String& type);
    void loadModel(const ModelEntry&);
    void importFiles(juce::StringArray);
    void importDownloaded(juce::File,juce::var provenance);
    void downloadModel(t3k::Model,juce::var provenance,std::function<void(juce::String)> completion={});
    void openToneLink(juce::String);
    void setMessage(juce::String);
    juce::String message() const;
    juce::AudioProcessorValueTreeState parameters;
    juce::SharedResourcePointer<Services> services;
    Engine engine;
    std::atomic<float> inputPeak{0},outputPeak{0};
    std::atomic<int> libraryVersion{0};
    std::function<void(int)> onOpenTone;
    std::function<void()> onAudioSettings;
private:
    void timerCallback() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
    juce::SmoothedValue<float> inGain,outGain;
    std::atomic<float>* inputParameter=nullptr;
    std::atomic<float>* outputParameter=nullptr;
    std::atomic<float>* bypassParameter=nullptr;
    mutable std::mutex messageMutex;
    juce::String notification;
    juce::ThreadPool imports{1};
    t3k::AsyncScope downloadCallbacks;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};
}
