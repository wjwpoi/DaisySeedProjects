#include "Editor.h"

namespace tonedock {
namespace {
const juce::Colour background{0xff111715},panel{0xff1b2420},ink{0xffecf2e8},muted{0xff93a79a},accent{0xffbfeb83};
void style(juce::TextEditor& e,const juce::String& placeholder) {
    e.setTextToShowWhenEmpty(placeholder,muted);e.setColour(juce::TextEditor::backgroundColourId,panel);
    e.setColour(juce::TextEditor::textColourId,ink);e.setColour(juce::TextEditor::outlineColourId,juce::Colour(0xff35443a));
}
juce::StringArray parameterNames(const juce::String& type) {
    if(type=="Gate")return {"Threshold","Release"};
    if(type=="Compressor")return {"Threshold","Ratio","Attack","Release"};
    if(type=="Drive")return {"Drive","Tone","Level"};
    if(type=="NAM" || type=="Gain")return {"Level"};
    if(type=="EQ")return {"Bass","Mid","Treble"};
    if(type=="Chorus")return {"Rate","Depth","Feedback","Mix"};
    if(type=="Delay")return {"Time","Feedback","Mix"};
    if(type=="Reverb")return {"Room","Damping","Mix","Width"};return {};
}
juce::String parameterText(const juce::String& t,int i,double v) {
    if(t=="NAM")return juce::String((v-.5)*24,1)+" dB";
    if(t=="Gain")return juce::String((v-.5)*48,1)+" dB";
    if(t=="EQ")return juce::String((v-.5)*24,1)+" dB";
    if(t=="Gate")return i==0?juce::String(-80+70*v,1)+" dB":juce::String(20+480*v,0)+" ms";
    if(t=="Compressor") {
        if(i==0)return juce::String(-48+48*v,1)+" dB";
        if(i==1)return juce::String(1+19*v,1)+":1";
        return juce::String(i==2?1+49*v:20+480*v,0)+" ms";
    }
    if(t=="Delay" && i==0)return juce::String(20+980*v,0)+" ms";
    if(t=="Chorus" && i==0)return juce::String(.1+4.9*v,2)+" Hz";
    if(t=="Drive") {
        if(i==0)return juce::String(1+39*v,1)+" x";
        if(i==1)return juce::String(500+9500*v,0)+" Hz";
        return juce::String((v-.5)*24,1)+" dB";
    }
    return juce::String(v*100,0)+" %";
}
}
struct Editor::ModelRows : juce::ListBoxModel {
    Editor& editor;explicit ModelRows(Editor& e):editor(e){}
    int getNumRows() override{return int(editor.filtered.size());}
    void paintListBoxItem(int row,juce::Graphics& g,int w,int h,bool selected) override {
        if(row<0 || row>=getNumRows())return;auto& m=editor.filtered[size_t(row)];
        g.setColour(selected?juce::Colour(0xff344734):panel);g.fillRoundedRectangle(4,2,float(w-8),float(h-4),6);
        g.setColour(ink);g.setFont(14);g.drawText((m.favorite?"* ":"")+m.name,13,5,w-26,23,juce::Justification::centredLeft);
        g.setColour(muted);g.setFont(11);g.drawText(m.category+"  /  "+m.architecture,13,29,w-26,17,juce::Justification::centredLeft);
    }
    void selectedRowsChanged(int row) override{editor.selectModel(row);}
    void listBoxItemDoubleClicked(int row,const juce::MouseEvent&) override{
        if(row>=0 && row<getNumRows())editor.processor.loadModel(editor.filtered[size_t(row)]);
    }
};
struct Editor::ChainRows : juce::ListBoxModel {
    Editor& editor;explicit ChainRows(Editor& e):editor(e){}
    int getNumRows() override{return int(editor.visibleChain.size());}
    void paintListBoxItem(int row,juce::Graphics& g,int w,int h,bool selected) override {
        if(row<0 || row>=getNumRows())return;auto& s=editor.visibleChain[size_t(row)];
        g.setColour(selected?juce::Colour(0xff30462c):panel);g.fillRoundedRectangle(2,3,float(w-4),float(h-6),8);
        g.setColour(s.controls->enabled.load()?accent:muted);g.fillEllipse(18,22,8,8);
        g.setFont(13);g.drawText(juce::String(row+1).paddedLeft('0',2),37,6,30,h-12,juce::Justification::centredLeft);
        g.setColour(ink);g.setFont(15);g.drawText(s.type,80,5,130,h-10,juce::Justification::centredLeft);
        g.setColour(muted);g.setFont(12);g.drawText(s.file.isEmpty() && (s.type=="NAM" || s.type=="Cabinet IR")?"Choose a model from your library":s.title,210,5,w-240,h-10,juce::Justification::centredLeft);
    }
    void selectedRowsChanged(int row) override{editor.selectStage(row);}
    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override {
        auto row=rows[0];return row>=0 && row<getNumRows()?juce::var("stage:"+editor.visibleChain[size_t(row)].id):juce::var();
    }
};

class Editor::ToneBrowser : public juce::Component,private juce::ListBoxModel {
public:
    explicit ToneBrowser(Processor& p,int toneId):processor(p),client(p.services->tone3000) {
        for(auto* component:std::initializer_list<juce::Component*>{&key,&connect,&browse,&disconnect,&feed,&architecture,&refresh,&previous,&next,&list,&detail,&models,&download,&source,&state})addAndMakeVisible(component);
        style(key,"TONE3000 publishable key: t3k_pub_...");key.setText(client.publishableKey());key.setReadOnly(client.publishableKey().isNotEmpty());
        connect.onClick=[this]{startFlow(t3k::FlowRequest::Kind::selectTone,0);};
        browse.onClick=[this]{startFlow(t3k::FlowRequest::Kind::selectTone,0);};
        disconnect.onClick=[this]{if(flow)flow->cancel();client.clearTokens();state.setText("Disconnected",juce::dontSendNotification);};
        for(auto name:juce::StringArray{"Latest","Trending","Favorites","Created","Downloaded"})feed.addItem(name,feed.getNumItems()+1);
        feed.setSelectedId(1);feed.onChange=[this]{page=1;loadFeed();};refresh.onClick=[this]{loadFeed();};
        previous.onClick=[this]{if(page>1){--page;loadFeed();}};next.onClick=[this]{++page;loadFeed();};
        architecture.addItem("A2 models",1);architecture.addItem("A1 / Custom models",2);architecture.setSelectedId(client.selectedArchitecture()==2?1:2);
        architecture.onChange=[this]{++selectionGeneration;client.setArchitecture(architecture.getSelectedId()==1?2:0);models.clear();download.setEnabled(false);};
        list.setModel(this);list.setRowHeight(42);list.setColour(juce::ListBox::backgroundColourId,background);
        style(detail,"Select a tone to inspect its creator, license and captures");detail.setMultiLine(true);detail.setReadOnly(true);
        models.onChange=[this]{download.setEnabled(models.getSelectedId()>0);};download.setEnabled(false);
        download.onClick=[this]{downloadSelected();};source.onClick=[this]{if(current.tone.url.startsWith("https://"))juce::URL(current.tone.url).launchInDefaultBrowser();};
        setSize(900,650);
        if(toneId>0)startFlow(t3k::FlowRequest::Kind::loadTone,toneId);
        else if(client.isConnected())loadFeed();
        else state.setText("Connect your account, then browse or open a tone link.",juce::dontSendNotification);
    }
    ~ToneBrowser() override{if(flow)flow->cancel();list.setModel(nullptr);}
    void paint(juce::Graphics& g) override{g.fillAll(background);g.setColour(accent);g.setFont(20);g.drawText("Powered by TONE3000",20,12,500,28,juce::Justification::centredLeft);}
    void resized() override {
        int w=getWidth();key.setBounds(20,52,w-370,30);connect.setBounds(w-340,52,95,30);browse.setBounds(w-235,52,100,30);disconnect.setBounds(w-125,52,105,30);
        feed.setBounds(20,96,140,28);architecture.setBounds(170,96,180,28);refresh.setBounds(360,96,85,28);previous.setBounds(455,96,65,28);next.setBounds(530,96,65,28);
        list.setBounds(20,140,350,getHeight()-204);detail.setBounds(390,140,w-410,getHeight()-300);
        models.setBounds(390,getHeight()-145,w-410,30);download.setBounds(390,getHeight()-100,170,32);source.setBounds(575,getHeight()-100,180,32);
        state.setBounds(20,getHeight()-48,w-40,35);
    }
private:
    int getNumRows() override{return tones.size();}
    void paintListBoxItem(int row,juce::Graphics& g,int w,int h,bool selected) override {
        if(row<0 || row>=tones.size())return;g.setColour(selected?juce::Colour(0xff344734):panel);g.fillRect(0,1,w,h-2);
        g.setColour(ink);g.setFont(13);g.drawText(tones[row].title,8,3,w-16,20,juce::Justification::centredLeft);
        g.setColour(muted);g.setFont(11);g.drawText(tones[row].user.creatorName()+" / "+tones[row].gear,8,23,w-16,16,juce::Justification::centredLeft);
    }
    void selectedRowsChanged(int row) override{if(row>=0 && row<tones.size())loadTone(tones[row].id);}
    bool readyKey() {
        if(client.publishableKey().isNotEmpty())return true;
        if(processor.services->configureKey(key.getText())){key.setReadOnly(true);return true;}
        state.setText("Enter a publishable t3k_pub_ key from TONE3000 Settings > API Keys. No secret key is needed.",juce::dontSendNotification);return false;
    }
    void startFlow(t3k::FlowRequest::Kind kind,int id) {
        if(!readyKey())return;
        if(flow)flow->cancel();flow=t3k::makeAuthFlow(t3k::FlowMode::systemBrowser);
        t3k::FlowRequest request;request.kind=kind;request.catalog.architecture=client.selectedArchitecture();request.catalog.preview=true;
        if(id>0)request.toneId=juce::String(id);
        state.setText("Continue in your browser. You will return here after choosing a tone.",juce::dontSendNotification);
        try{flow->start(request,scope.wrap([this](t3k::FlowResult result){
            if(result.connected()) {
                state.setText("Connected",juce::dontSendNotification);
                if(result.toneId.containsOnly("0123456789") && result.toneId.getIntValue()>0)loadTone(result.toneId.getIntValue());else loadFeed();
            }else state.setText(result.canceled()?"Canceled":result.error,juce::dontSendNotification);
        }));}catch(const std::exception& e){state.setText(e.what(),juce::dontSendNotification);}
    }
    void loadFeed() {
        if(!client.isConnected()){state.setText("Connect TONE3000 first",juce::dontSendNotification);return;}
        state.setText("Loading page "+juce::String(page)+"...",juce::dontSendNotification);
        auto done=scope.wrap([this](t3k::Result<t3k::Page<t3k::Tone>> result){
            if(!result.ok()){state.setText(result.error.userMessage(),juce::dontSendNotification);return;}
            tones=result->data;list.updateContent();previous.setEnabled(page>1);next.setEnabled(page<result->totalPages);
            state.setText("Page "+juce::String(page)+" / "+juce::String(result->totalPages),juce::dontSendNotification);
        });
        t3k::ListLibraryParams params;params.page=page;params.pageSize=30;
        if(feed.getSelectedId()==3)client.listFavoritedTones(params,done);
        else if(feed.getSelectedId()==4)client.listCreatedTones(params,done);
        else if(feed.getSelectedId()==5)client.listDownloadedTones(params,done);
        else {
            auto callback=scope.wrap([this](t3k::Result<juce::Array<t3k::Tone>> result){
                if(!result.ok()){state.setText(result.error.userMessage(),juce::dontSendNotification);return;}
                tones=*result;list.updateContent();previous.setEnabled(false);next.setEnabled(false);state.setText(juce::String(tones.size())+" tones",juce::dontSendNotification);
            });
            if(feed.getSelectedId()==2)client.listTrendingTones({},callback);else client.listLatestTones(callback);
        }
    }
    void loadTone(int id) {
        auto ticket=++selectionGeneration;state.setText("Loading tone and captures...",juce::dontSendNotification);models.clear();download.setEnabled(false);
        client.getToneWithModels(id,scope.wrap([this,ticket](t3k::Result<t3k::ToneWithModels> result){
            if(ticket!=selectionGeneration)return;
            if(!result.ok()){state.setText(result.error.userMessage(),juce::dontSendNotification);return;}
            current=*result;models.clear();for(auto& m:current.models)models.addItem(m.name,models.getNumItems()+1);
            detail.setText(current.tone.title+"\n\nBy "+current.tone.user.creatorName()+"\n"+current.tone.user.url+"\n\n"+current.tone.description
                +"\n\nGear: "+current.tone.gear+"\nLicense: "+t3k::licenseLabel(current.tone.license)+"\n\n"+current.tone.url);
            if(models.getNumItems()>0)models.setSelectedId(1);
            state.setText(current.models.isEmpty()?"No supported captures for this architecture. Try A1 / Custom or A2.":"Choose a capture to download into your library.",juce::dontSendNotification);
        }));
    }
    void downloadSelected() {
        auto row=models.getSelectedId()-1;if(row<0 || row>=current.models.size())return;
        auto tone=current.tone;auto model=current.models[row];download.setEnabled(false);state.setText("Downloading "+model.name+"...",juce::dontSendNotification);
        auto* meta=new juce::DynamicObject;meta->setProperty("name",tone.title+" / "+model.name);meta->setProperty("author",tone.user.creatorName());
            meta->setProperty("license",tone.license);meta->setProperty("sourceUrl",tone.url);meta->setProperty("toneId",juce::String(tone.id));
            meta->setProperty("category",tone.gear=="amp" || tone.gear=="amp-cab"?"Amplifier":tone.gear=="pedal"?"Pedal":tone.format=="ir"?"Cabinet IR":"Unsorted");
            juce::Array<juce::var> tags;for(auto& t:tone.tags)tags.add(t.name);for(auto& make:tone.makes)tags.add(make.name);meta->setProperty("tags",tags);
        processor.downloadModel(model,juce::var(meta),scope.wrap([this](juce::String status){download.setEnabled(true);state.setText(status,juce::dontSendNotification);}));
    }
    Processor& processor;t3k::Client& client;t3k::AsyncScope scope;
    std::unique_ptr<t3k::AuthFlow> flow;
    juce::TextEditor key,detail;juce::TextButton connect{"Connect"},browse{"Browse"},disconnect{"Disconnect"},refresh{"Refresh"},previous{"Prev"},next{"Next"},download{"Download to library"},source{"View on TONE3000"};
    juce::ComboBox feed,architecture,models;juce::ListBox list;juce::Label state;
    juce::Array<t3k::Tone> tones;t3k::ToneWithModels current;int page=1,selectionGeneration=0;
};

Editor::Editor(Processor& p):AudioProcessorEditor(p),processor(p) {
    theme.setColour(juce::ResizableWindow::backgroundColourId,background);theme.setColour(juce::TextButton::buttonColourId,panel);
    theme.setColour(juce::TextButton::textColourOffId,ink);theme.setColour(juce::ComboBox::backgroundColourId,panel);
    theme.setColour(juce::Slider::rotarySliderFillColourId,accent);theme.setColour(juce::Slider::thumbColourId,accent);
    setLookAndFeel(&theme);
    for(auto* c:std::initializer_list<juce::Component*>{&libraryList,&chainList,&search,&categoryEdit,&tagsEdit,&link,&categoryFilter,&effectChoice,&importButton,&cloudButton,&loadButton,&favoriteButton,&metadataButton,&removeModelButton,&addButton,&deleteButton,&upButton,&downButton,&bypassStageButton,&audioButton,&saveButton,&openButton,&linkButton,&inputGain,&outputGain,&bypass})addAndMakeVisible(c);
    modelRows=std::make_unique<ModelRows>(*this);chainRows=std::make_unique<ChainRows>(*this);
    libraryList.setModel(modelRows.get());libraryList.setRowHeight(51);libraryList.setColour(juce::ListBox::backgroundColourId,background);
    chainList.setModel(chainRows.get());chainList.setRowHeight(52);chainList.setColour(juce::ListBox::backgroundColourId,background);
    style(search,"Search names, tags, creator...");style(categoryEdit,"Category");style(tagsEdit,"Tags, comma separated");style(link,"Paste a TONE3000 tone page URL");
    search.onTextChange=[this]{refreshLibrary();};categoryFilter.onChange=[this]{refreshLibrary();};
    categoryFilter.addItem("All models",1);categoryFilter.addItem("Favorites",2);categoryFilter.setSelectedId(1);
    for(auto& t:effectTypes)effectChoice.addItem(t,effectChoice.getNumItems()+1);effectChoice.setSelectedId(3);
    importButton.onClick=[this]{chooseImport();};cloudButton.onClick=[this]{openTone();};linkButton.onClick=[this]{processor.openToneLink(link.getText());};
    loadButton.onClick=[this]{auto row=libraryList.getSelectedRow();if(row>=0 && row<int(filtered.size()))processor.loadModel(filtered[size_t(row)]);};
    favoriteButton.onClick=[this]{auto row=libraryList.getSelectedRow();if(row<0 || row>=int(filtered.size()))return;
        try{auto m=filtered[size_t(row)];m.favorite=!m.favorite;processor.services->library.update(m);++processor.libraryVersion;}
        catch(const std::exception& e){processor.setMessage(e.what());}};
    metadataButton.onClick=[this]{auto row=libraryList.getSelectedRow();if(row<0 || row>=int(filtered.size()))return;
        try{auto m=filtered[size_t(row)];m.category=categoryEdit.getText().trim();m.tags=juce::StringArray::fromTokens(tagsEdit.getText(),",","");m.tags.trim();m.tags.removeEmptyStrings();processor.services->library.update(m);++processor.libraryVersion;}
        catch(const std::exception& e){processor.setMessage(e.what());}};
    removeModelButton.onClick=[this]{auto row=libraryList.getSelectedRow();if(row<0 || row>=int(filtered.size()))return;
        try{processor.services->library.remove(filtered[size_t(row)].id);++processor.libraryVersion;}catch(const std::exception& e){processor.setMessage(e.what());}};
    addButton.onClick=[this]{processor.addEffect(effectChoice.getText());};
    deleteButton.onClick=[this]{editChain([this](auto& chain){std::erase_if(chain,[this](const auto& s){return s.id==selectedStageId;});});};
    upButton.onClick=[this]{editChain([this](auto& chain){for(size_t i=1;i<chain.size();++i)if(chain[i].id==selectedStageId){std::swap(chain[i],chain[i-1]);break;}});};
    downButton.onClick=[this]{editChain([this](auto& chain){for(size_t i=0;i+1<chain.size();++i)if(chain[i].id==selectedStageId){std::swap(chain[i],chain[i+1]);break;}});};
    bypassStageButton.onClick=[this]{for(auto& s:processor.engine.chain())if(s.id==selectedStageId)s.controls->enabled.store(!s.controls->enabled.load());chainList.repaint();};
    audioButton.onClick=[this]{if(processor.onAudioSettings)processor.onAudioSettings();else processor.setMessage("Select input, sample rate and buffer size in your DAW.");};
    saveButton.onClick=[this]{preset(true);};openButton.onClick=[this]{preset(false);};
    for(auto* slider:{&inputGain,&outputGain}){slider->setSliderStyle(juce::Slider::LinearHorizontal);slider->setTextBoxStyle(juce::Slider::TextBoxRight,false,65,22);slider->setTextValueSuffix(" dB");}
    inputAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters,"input",inputGain);
    outputAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters,"output",outputGain);
    bypassAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.parameters,"bypass",bypass);
    for(int i=0;i<4;++i){auto& k=knobs[size_t(i)];addAndMakeVisible(k);addAndMakeVisible(knobLabels[size_t(i)]);k.setRange(0,1,.001);k.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);k.setTextBoxStyle(juce::Slider::TextBoxBelow,false,100,22);
        knobLabels[size_t(i)].setJustificationType(juce::Justification::centred);k.onValueChange=[this,i]{for(auto& s:visibleChain)if(s.id==selectedStageId)s.controls->values[size_t(i)].store(float(knobs[size_t(i)].getValue()));};}
    processor.onOpenTone=[this](int id){openTone(id);};setResizable(true,true);setResizeLimits(1120,780,1700,1100);setSize(1140,780);
    refreshLibrary();visibleChain=processor.engine.chain();chainList.updateContent();chainList.selectRow(1);startTimerHz(15);
}
Editor::~Editor(){processor.onOpenTone=nullptr;stopTimer();browser.reset();libraryList.setModel(nullptr);chainList.setModel(nullptr);setLookAndFeel(nullptr);}
void Editor::paint(juce::Graphics& g) {
    g.fillAll(background);g.setColour(panel);g.fillRect(0,0,getWidth(),91);g.fillRect(0,getHeight()-45,getWidth(),45);
    g.setColour(accent);g.setFont(30);g.drawText("ToneDock",22,17,250,37,juce::Justification::centredLeft);
    g.setColour(muted);g.setFont(11);g.drawText("YOUR TONES. YOUR SIGNAL.",24,57,250,19,juce::Justification::centredLeft);
    g.setColour(ink);g.setFont(13);g.drawText("MODEL LIBRARY",22,105,250,24,juce::Justification::centredLeft);
    g.drawText("SIGNAL CHAIN",344,105,200,24,juce::Justification::centredLeft);
    g.setColour(muted);g.setFont(11);g.drawText("Drag effects to reorder. Select a row to edit.",344,134,600,20,juce::Justification::centredLeft);
    g.drawText("INPUT",330,14,70,20,juce::Justification::centredLeft);g.drawText("OUTPUT",565,14,80,20,juce::Justification::centredLeft);
    for(int c=0;c<2;++c){float level=c==0?processor.inputPeak.load():processor.outputPeak.load();int x=c==0?330:565;
        g.setColour(juce::Colour(0xff35443a));g.fillRoundedRectangle(float(x),72,190,4,2);g.setColour(level>=.99f?juce::Colours::orange:accent);g.fillRoundedRectangle(float(x),72,190*juce::jlimit(0.0f,1.0f,level),4,2);}
    g.setColour(juce::Colour(0xff35443a));g.drawVerticalLine(321,100,float(getHeight()-60));
    g.setColour(muted);g.setFont(12);g.drawText(displayStatus,22,getHeight()-39,getWidth()-260,30,juce::Justification::centredLeft);
    g.drawText(juce::String(processor.engine.latency())+" samples DSP latency",getWidth()-240,getHeight()-39,220,30,juce::Justification::centredRight);
    if(filtered.empty()){g.setColour(muted);g.setFont(13);g.drawFittedText("Drop .nam / IR .wav / .zip here\nor import from TONE3000",30,260,275,100,juce::Justification::centred,4);}
    for(auto& s:visibleChain)if(s.id==selectedStageId && (s.type=="NAM" || s.type=="Cabinet IR")) {
        g.setColour(muted);g.setFont(14);
        g.drawFittedText(s.type=="NAM"?"Choose an amplifier or pedal capture from your library.\n\nFor an amp-only capture, add a cabinet IR after NAM.":"Choose a WAV impulse response from your library.\n\nA capture that already includes a cabinet usually needs no additional cabinet block.",550,498,getWidth()-600,110,juce::Justification::centredLeft,5);
    }
}
void Editor::resized() {
    int w=getWidth(),h=getHeight();inputGain.setBounds(325,36,200,30);outputGain.setBounds(560,36,200,30);bypass.setBounds(w-355,35,90,28);audioButton.setBounds(w-250,34,125,30);cloudButton.setBounds(w-115,34,95,30);
    search.setBounds(22,141,277,29);categoryFilter.setBounds(22,180,277,28);libraryList.setBounds(18,220,286,h-467);
    loadButton.setBounds(22,h-237,134,29);favoriteButton.setBounds(164,h-237,135,29);categoryEdit.setBounds(22,h-199,277,27);tagsEdit.setBounds(22,h-164,277,27);
    metadataButton.setBounds(22,h-128,132,28);removeModelButton.setBounds(165,h-128,134,28);importButton.setBounds(22,h-88,277,29);
    effectChoice.setBounds(w-340,104,205,29);addButton.setBounds(w-125,104,100,29);chainList.setBounds(340,165,w-365,252);
    bypassStageButton.setBounds(344,430,145,29);upButton.setBounds(500,430,60,29);downButton.setBounds(570,430,70,29);deleteButton.setBounds(650,430,75,29);
    for(int i=0;i<4;++i){int width=juce::jmin(170,(w-390)/4),x=346+i*width;knobLabels[size_t(i)].setBounds(x,480,width,24);knobs[size_t(i)].setBounds(x,507,width,130);}
    link.setBounds(344,h-124,w-500,28);linkButton.setBounds(w-140,h-124,115,28);openButton.setBounds(344,h-84,125,28);saveButton.setBounds(480,h-84,125,28);
}
void Editor::refreshLibrary() {
    try {
        auto selectedRow=libraryList.getSelectedRow();
        auto selectedId=selectedRow>=0 && selectedRow<int(filtered.size())?filtered[size_t(selectedRow)].id:juce::String();
        auto all=processor.services->library.entries();auto query=search.getText().trim();auto filter=categoryFilter.getText();auto id=categoryFilter.getSelectedId();
        juce::StringArray categories;for(auto& m:all)categories.addIfNotAlreadyThere(m.category);categories.sort(true);
        categoryFilter.clear(juce::dontSendNotification);categoryFilter.addItem("All models",1);categoryFilter.addItem("Favorites",2);for(int i=0;i<categories.size();++i)categoryFilter.addItem(categories[i],i+3);
        if(id<=2)categoryFilter.setSelectedId(juce::jmax(1,id),juce::dontSendNotification);else categoryFilter.setText(filter,juce::dontSendNotification);
        filtered.clear();for(auto& m:all) {
            if(id==2 && !m.favorite)continue;if(id>2 && m.category!=filter)continue;
            if(query.isNotEmpty() && !(m.name+" "+m.category+" "+m.author+" "+m.tags.joinIntoString(" ")).containsIgnoreCase(query))continue;
            filtered.push_back(m);
        }
        std::sort(filtered.begin(),filtered.end(),[](const auto& a,const auto& b){return a.name.compareIgnoreCase(b.name)<0;});
        libraryList.updateContent();
        auto selected=std::find_if(filtered.begin(),filtered.end(),[&](const auto& m){return m.id==selectedId;});
        if(selected!=filtered.end()){auto row=int(std::distance(filtered.begin(),selected));libraryList.selectRow(row);selectModel(row);}
        else{libraryList.deselectAllRows();categoryEdit.clear();tagsEdit.clear();favoriteButton.setButtonText("Favorite");}
        version=processor.libraryVersion.load();libraryModified=processor.services->library.directory().getChildFile("library.json").getLastModificationTime().toMilliseconds();repaint();
    }catch(const std::exception& e){processor.setMessage(e.what());}
}
void Editor::selectModel(int row) {
    if(row<0 || row>=int(filtered.size()))return;auto& m=filtered[size_t(row)];categoryEdit.setText(m.category);tagsEdit.setText(m.tags.joinIntoString(", "));
    favoriteButton.setButtonText(m.favorite?"Unfavorite":"Favorite");
}
void Editor::selectStage(int row) {
    if(row<0 || row>=int(visibleChain.size()))return;auto& s=visibleChain[size_t(row)];selectedStageId=s.id;auto names=parameterNames(s.type);
    for(int i=0;i<4;++i){auto& k=knobs[size_t(i)];k.setEnabled(i<names.size());k.setVisible(i<names.size());knobLabels[size_t(i)].setVisible(i<names.size());knobLabels[size_t(i)].setText(i<names.size()?names[i]:"",juce::dontSendNotification);
        k.textFromValueFunction=[t=s.type,i](double value){return parameterText(t,i,value);};k.setValue(s.controls->values[size_t(i)].load(),juce::dontSendNotification);}
}
void Editor::editChain(const std::function<void(std::vector<StageSpec>&)>& fn){auto chain=processor.engine.chain();fn(chain);processor.engine.setChain(std::move(chain));}
bool Editor::isInterestedInDragSource(const SourceDetails& details){return details.description.toString().startsWith("stage:");}
void Editor::itemDropped(const SourceDetails& d) {
    auto id=d.description.toString().fromFirstOccurrenceOf("stage:",false,false);
    auto y=d.localPosition.y-chainList.getY()+chainList.getViewport()->getViewPositionY();
    int target=juce::jmax(0,int(y)/chainList.getRowHeight());
    editChain([&](auto& chain){auto it=std::find_if(chain.begin(),chain.end(),[&](const auto& s){return s.id==id;});if(it==chain.end())return;auto moving=*it;auto source=int(std::distance(chain.begin(),it));chain.erase(it);if(target>source)--target;chain.insert(chain.begin()+juce::jlimit(0,int(chain.size()),target),moving);});
}
void Editor::chooseImport() {
    chooser=std::make_unique<juce::FileChooser>("Import NAM models, IRs or ZIP bundles",juce::File(),"*.nam;*.wav;*.zip");
    chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::canSelectDirectories|juce::FileBrowserComponent::canSelectMultipleItems,
        scope.wrap([this](const juce::FileChooser& selected){juce::StringArray paths;for(auto& f:selected.getResults())paths.add(f.getFullPathName());if(!paths.isEmpty())processor.importFiles(paths);}));
}
void Editor::preset(bool save) {
    chooser=std::make_unique<juce::FileChooser>(save?"Save ToneDock preset":"Open ToneDock preset",juce::File(),"*.tonedock");
    chooser->launchAsync((save?juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::warnAboutOverwriting:juce::FileBrowserComponent::openMode)|juce::FileBrowserComponent::canSelectFiles,
        scope.wrap([this,save](const juce::FileChooser& selected){auto file=selected.getResult();if(file==juce::File())return;juce::MemoryBlock data;
            if(save){processor.getStateInformation(data);if(!file.withFileExtension("tonedock").replaceWithData(data.getData(),data.getSize()))processor.setMessage("Cannot save preset");}
            else if(file.getSize()<=1024*1024 && file.loadFileAsData(data))processor.setStateInformation(data.getData(),int(data.getSize()));}));
}
void Editor::openTone(int id) {
    juce::DialogWindow::LaunchOptions options;options.dialogTitle="ToneDock / TONE3000";options.dialogBackgroundColour=background;
    options.content.setOwned(new ToneBrowser(processor,id));options.componentToCentreAround=this;options.useNativeTitleBar=true;options.resizable=true;
    browser.reset(options.create());browser->setLookAndFeel(&theme);browser->setVisible(true);
}
void Editor::timerCallback() {
    if(version!=processor.libraryVersion.load())refreshLibrary();
    if(++refreshTick%15==0 && libraryModified!=processor.services->library.directory().getChildFile("library.json").getLastModificationTime().toMilliseconds())refreshLibrary();
    auto nextChain=processor.engine.chain();bool changed=nextChain.size()!=visibleChain.size();
    if(!changed)for(size_t i=0;i<nextChain.size();++i)if(nextChain[i].id!=visibleChain[i].id || nextChain[i].title!=visibleChain[i].title || nextChain[i].controls!=visibleChain[i].controls){changed=true;break;}
    if(changed){visibleChain=std::move(nextChain);chainList.updateContent();for(size_t i=0;i<visibleChain.size();++i)if(visibleChain[i].id==selectedStageId){chainList.selectRow(int(i));selectStage(int(i));break;}}
    auto state=processor.engine.status();displayStatus=state=="Ready"?processor.message():state;if(displayStatus.isEmpty())displayStatus="Ready / import a NAM model to begin";
    if(processor.getLatencySamples()!=processor.engine.latency())processor.setLatencySamples(processor.engine.latency());repaint();
}
}
