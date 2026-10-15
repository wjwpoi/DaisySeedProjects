#pragma once
#include "Processor.h"
#include "AuthFlow.h"

namespace tonedock {
class Editor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget,
    public juce::TextDragAndDropTarget,public juce::DragAndDropContainer,public juce::DragAndDropTarget,private juce::Timer {
public:
    explicit Editor(Processor&);
    ~Editor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override{return true;}
    void filesDropped(const juce::StringArray& files,int,int) override{processor.importFiles(files);}
    bool isInterestedInTextDrag(const juce::String& text) override{return parseToneLink(text).has_value();}
    void textDropped(const juce::String& text,int,int) override{processor.openToneLink(text);}
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDropped(const SourceDetails&) override;
    void openTone(int id=0);
private:
    struct ModelRows;struct ChainRows;class ToneBrowser;
    void timerCallback() override;
    void refreshLibrary();void selectModel(int);void selectStage(int);
    void editChain(const std::function<void(std::vector<StageSpec>&)>&);
    void chooseImport();void preset(bool save);
    Processor& processor;
    juce::LookAndFeel_V4 theme;
    std::unique_ptr<ModelRows> modelRows;std::unique_ptr<ChainRows> chainRows;
    juce::ListBox libraryList,chainList;
    juce::TextEditor search,categoryEdit,tagsEdit,link;
    juce::ComboBox categoryFilter,effectChoice;
    juce::TextButton importButton{"Import"},cloudButton{"TONE3000"},loadButton{"Load model"},favoriteButton{"Favorite"},
        metadataButton{"Save tags"},removeModelButton{"Remove"},addButton{"+ Add"},deleteButton{"Delete"},upButton{"Up"},downButton{"Down"},
        bypassStageButton{"Enable / bypass"},audioButton{"Audio settings"},saveButton{"Save preset"},openButton{"Open preset"},linkButton{"Open link"};
    juce::Slider inputGain,outputGain;
    juce::ToggleButton bypass{"Bypass"};
    std::array<juce::Slider,4> knobs;std::array<juce::Label,4> knobLabels;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputAttachment,outputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::DialogWindow> browser;
    std::vector<ModelEntry> filtered;
    std::vector<StageSpec> visibleChain;
    juce::String selectedStageId,displayStatus;
    int version=-1;
    int refreshTick=0;
    juce::int64 libraryModified=0;
    t3k::AsyncScope scope;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};
}
