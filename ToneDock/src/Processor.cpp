#include "Processor.h"
#include "Editor.h"
#include <cmath>

namespace tonedock {
Services::Services() {
    auto settings=juce::JSON::parse(library.directory().getChildFile("settings.json"));
    tone3000.configurePublishableKey(settings.getProperty("publishableKey","").toString());
}
bool Services::configureKey(const juce::String& key) {
    if(!tone3000.configurePublishableKey(key.trim()))return false;
    auto* o=new juce::DynamicObject;o->setProperty("publishableKey",key.trim());
    return library.directory().getChildFile("settings.json").replaceWithText(juce::JSON::toString(juce::var(o)));
}
juce::AudioProcessorValueTreeState::ParameterLayout Processor::makeParameters() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"input",1},"Input gain",-36.0f,24.0f,0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"output",1},"Output gain",-60.0f,12.0f,-12.0f));
    p.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"Bypass",false));
    return {p.begin(),p.end()};
}
Processor::Processor() : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
    .withOutput("Output",juce::AudioChannelSet::stereo(),true)),parameters(*this,nullptr,"ToneDock",makeParameters()) {
    inputParameter=parameters.getRawParameterValue("input");outputParameter=parameters.getRawParameterValue("output");bypassParameter=parameters.getRawParameterValue("bypass");
    for(auto& type:juce::StringArray{"Gate","NAM","Cabinet IR","EQ"})addEffect(type);
    startTimerHz(10);
}
Processor::~Processor() { stopTimer();imports.removeAllJobs(true,-1); }
void Processor::timerCallback(){if(getLatencySamples()!=engine.latency())setLatencySamples(engine.latency());}
bool Processor::isBusesLayoutSupported(const BusesLayout& b) const {
    return b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()
        && (b.getMainInputChannelSet()==juce::AudioChannelSet::mono() || b.getMainInputChannelSet()==juce::AudioChannelSet::stereo());
}
void Processor::prepareToPlay(double sr,int) {
    inGain.reset(sr,.02);outGain.reset(sr,.02);
    inGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(inputParameter->load()));
    outGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(outputParameter->load()));
    engine.prepare(sr);
}
void Processor::processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer&) {
    juce::ScopedNoDenormals denormals;
    for(int c=getTotalNumInputChannels();c<audio.getNumChannels();++c)audio.clear(c,0,audio.getNumSamples());
    if(audio.getNumChannels()<2)return;
    if(getTotalNumInputChannels()==1)audio.copyFrom(1,0,audio,0,0,audio.getNumSamples());
    inputPeak.store(juce::jmax(audio.getMagnitude(0,0,audio.getNumSamples()),audio.getMagnitude(1,0,audio.getNumSamples())));
    if(bypassParameter->load()>=.5f){outputPeak.store(inputPeak.load());return;}
    inGain.setTargetValue(juce::Decibels::decibelsToGain(inputParameter->load()));
    outGain.setTargetValue(juce::Decibels::decibelsToGain(outputParameter->load()));
    for(int i=0;i<audio.getNumSamples();++i) {
        auto gain=inGain.getNextValue();for(int c=0;c<2;++c)audio.setSample(c,i,std::isfinite(audio.getSample(c,i))?audio.getSample(c,i)*gain:0.0f);
    }
    engine.process(audio);
    for(int i=0;i<audio.getNumSamples();++i) {
        auto gain=outGain.getNextValue();for(int c=0;c<2;++c) {
            auto x=audio.getSample(c,i)*gain;audio.setSample(c,i,std::isfinite(x)?juce::jlimit(-1.0f,1.0f,x):0.0f);
        }
    }
    outputPeak.store(juce::jmax(audio.getMagnitude(0,0,audio.getNumSamples()),audio.getMagnitude(1,0,audio.getNumSamples())));
}
void Processor::setMessage(juce::String value) { std::lock_guard lock(messageMutex);notification=std::move(value); }
juce::String Processor::message() const { std::lock_guard lock(messageMutex);return notification; }
void Processor::addEffect(const juce::String& type) {
    auto chain=engine.chain();if(chain.size()>=16){setMessage("Maximum 16 effects");return;}
    StageSpec s;s.id=juce::Uuid().toString();s.type=type;s.title=type;
    if(type=="Reverb"){s.controls->values[2]=.2f;}if(type=="Delay"){s.controls->values[1]=.3f;s.controls->values[2]=.25f;}
    chain.push_back(std::move(s));engine.setChain(std::move(chain));
}
void Processor::loadModel(const ModelEntry& entry) {
    auto chain=engine.chain();auto type=entry.kind=="ir"?juce::String("Cabinet IR"):juce::String("NAM");
    auto it=std::find_if(chain.begin(),chain.end(),[&](const auto& s){return s.type==type;});
    if(it==chain.end()) {
        if(chain.size()>=16){setMessage("Remove an effect before adding this model");return;}
        StageSpec spec;spec.id=juce::Uuid().toString();spec.type=type;chain.push_back(spec);it=std::prev(chain.end());
    }
    it->file=services->library.fileFor(entry).getFullPathName();it->title=entry.name;
    engine.setChain(std::move(chain));setMessage("Model selected: "+entry.name);
}
void Processor::importFiles(juce::StringArray files) {
    setMessage("Importing and validating models...");
    imports.addJob([this,files=std::move(files)] {
        int count=0;juce::String failures;
        for(const auto& path:files)try {
            juce::File file(path);
            if(file.isDirectory()) {
                auto children=file.findChildFiles(juce::File::findFiles,true,"*.nam;*.wav;*.zip");
                if(children.size()>256)throw std::runtime_error("Folder exceeds 256 files per import");
                for(auto& child:children) {
                    if(child.hasFileExtension("zip"))count+=int(services->library.importArchive(child).size());
                    else{services->library.importFile(child);++count;}
                }
            } else if(file.hasFileExtension("zip"))count+=int(services->library.importArchive(file).size());
            else{services->library.importFile(file);++count;}
        }catch(const std::exception& e){failures+=juce::String(e.what())+"; ";}
        ++libraryVersion;setMessage("Imported / found "+juce::String(count)+" model(s)"+(failures.isNotEmpty()?". "+failures:juce::String()));
    });
}
void Processor::importDownloaded(juce::File file,juce::var provenance) {
    imports.addJob([this,file,provenance] {
        try{auto entry=services->library.importFile(file,provenance);++libraryVersion;setMessage("Downloaded to library: "+entry.name);}
        catch(const std::exception& e){file.deleteFile();setMessage("Download import failed: "+juce::String(e.what()));}
    });
}
void Processor::downloadModel(t3k::Model model,juce::var provenance,std::function<void(juce::String)> completion) {
    setMessage("Downloading "+model.name);
    services->tone3000.downloadModelFile(model,downloadCallbacks.wrap([this,provenance,completion](t3k::Result<juce::File> result){
        juce::String status;
        if(result.ok()){importDownloaded(*result,provenance);status="Download complete; validating and adding to library.";}
        else{status=result.error.userMessage();setMessage(status);}
        if(completion)completion(status);
    }));
}
void Processor::openToneLink(juce::String link) {
    if(auto id=parseToneLink(link)){if(onOpenTone)onOpenTone(*id);else setMessage("Open the ToneDock editor to load this TONE3000 link");}
    else setMessage("Use a TONE3000 tone page or tonedock://tone/<numeric-id>");
}
void Processor::getStateInformation(juce::MemoryBlock& out) {
    auto* object=new juce::DynamicObject;object->setProperty("version",1);
    object->setProperty("parameters",parameters.copyState().toXmlString());
    juce::Array<juce::var> chain;for(auto& s:engine.chain())chain.add(s.toVar());object->setProperty("chain",chain);
    auto json=juce::JSON::toString(juce::var(object));out.replaceAll(json.toRawUTF8(),json.getNumBytesAsUTF8());
}
void Processor::setStateInformation(const void* data,int size) {
    if(size<=0 || size>1024*1024){setMessage("Preset is empty or too large");return;}
    auto value=juce::JSON::parse(juce::String::fromUTF8(static_cast<const char*>(data),size));
    auto chain=value.getProperty("chain",{});if(!chain.isArray() || chain.size()>16){setMessage("Invalid ToneDock preset");return;}
    try {
        std::vector<StageSpec> specs;
        for(auto& v:*chain.getArray()){auto s=StageSpec::fromVar(v);if(!effectTypes.contains(s.type))throw std::runtime_error("Unknown effect in preset");specs.push_back(std::move(s));}
        auto xml=juce::parseXML(value.getProperty("parameters","").toString());
        if(!xml)throw std::runtime_error("Missing preset parameters");
        parameters.replaceState(juce::ValueTree::fromXml(*xml));engine.setChain(std::move(specs));setMessage("Preset restored");
    }catch(const std::exception& e){setMessage(e.what());}
}
juce::AudioProcessorEditor* Processor::createEditor(){return new Editor(*this);}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new tonedock::Processor;}
