#include "Engine.h"
#include "Library.h"
#include <NAM/get_dsp.h>
// AudioDSPTools' iPlug-derived resampler expects these host constants.
namespace iplug { inline constexpr double PI=juce::MathConstants<double>::pi; }
inline constexpr int DEFAULT_BLOCK_SIZE=256;
#include <ResamplingContainer/ResamplingContainer.h>
#include <cmath>

namespace tonedock {
namespace {
constexpr int chunkSize = 256;
constexpr int fadeSamples = 1024;
struct Biquad {
    std::array<float,6> a{{1,0,0,1,0,0}};
    float z1[2]{},z2[2]{};
    float tick(float x,int c) {
        auto y=(a[0]*x+z1[c])/a[3];
        z1[c]=a[1]*x-a[4]*y+z2[c]; z2[c]=a[2]*x-a[5]*y; return y;
    }
};
struct Node {
    StageSpec spec;
    double sr;
    std::unique_ptr<nam::DSP> model;
    std::unique_ptr<dsp::ResamplingContainer<float,1,12>> resampler;
    dsp::ResamplingContainer<float,1,12>::BlockProcessFunc modelFunction;
    std::array<float,chunkSize> mono{},modelOut{};
    juce::dsp::Convolution convolution;
    juce::dsp::Compressor<float> compressor;
    juce::dsp::Chorus<float> chorus;
    juce::dsp::DelayLine<float> delay;
    juce::Reverb reverb;
    Biquad eq[3];
    float envelope[2]{},gateGain[2]{},toneState[2]{};
    int latency=0;
    Node(StageSpec s,double sampleRate) : spec(std::move(s)),sr(sampleRate),delay(int(sampleRate*1.2)+2) {
        const juce::dsp::ProcessSpec prepareSpec{sr,chunkSize,2};
        if (spec.type=="NAM" && spec.file.isNotEmpty()) {
            model=nam::get_dsp(std::filesystem::u8path(spec.file.toStdString()),nam::DspLoadOptions{false});
            if (model->NumInputChannels()!=1 || model->NumOutputChannels()!=1) throw std::runtime_error("Only mono NAM captures are supported");
            auto trainingRate=model->GetExpectedSampleRate();
            if (trainingRate<=0) trainingRate=48000;
            if (model->SupportsArbitrarySampleRate()) trainingRate=sr;
            if (trainingRate<8000 || trainingRate>384000) throw std::runtime_error("Unsupported NAM training sample rate");
            model->Reset(trainingRate,int(std::ceil(chunkSize*trainingRate/sr))+128);
            modelFunction=[this](float** in,float** out,int n){model->process(in,out,n);};
            if (std::abs(trainingRate-sr)>0.5) {
                resampler=std::make_unique<dsp::ResamplingContainer<float,1,12>>(trainingRate);
                resampler->Reset(sr,chunkSize); latency=resampler->GetLatency();
            }
        } else if (spec.type=="Cabinet IR" && spec.file.isNotEmpty()) {
            juce::AudioFormatManager formats; formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(spec.file)));
            if (!reader || reader->numChannels>2 || reader->lengthInSamples<=0 || reader->lengthInSamples>reader->sampleRate*2)
                throw std::runtime_error("IR must be a mono/stereo WAV of at most two seconds");
            juce::AudioBuffer<float> ir(int(reader->numChannels),int(reader->lengthInSamples));
            if (!reader->read(&ir,0,ir.getNumSamples(),0,true,true)) throw std::runtime_error("Cannot decode IR");
            convolution.loadImpulseResponse(std::move(ir),reader->sampleRate,juce::dsp::Convolution::Stereo::yes,
                juce::dsp::Convolution::Trim::no,juce::dsp::Convolution::Normalise::no);
            convolution.prepare(prepareSpec); latency=convolution.getLatency();
        }
        if(spec.type=="Compressor") compressor.prepare(prepareSpec);
        if(spec.type=="Chorus") chorus.prepare(prepareSpec);
        if(spec.type=="Delay") delay.prepare(prepareSpec);
        if(spec.type=="Reverb") reverb.setSampleRate(sr);
    }
    void process(juce::AudioBuffer<float>& buffer,int n) {
        if(!spec.controls->enabled.load(std::memory_order_relaxed)) return;
        std::array<float,4> p;
        for(int i=0;i<4;++i) p[size_t(i)]=std::clamp(spec.controls->values[size_t(i)].load(std::memory_order_relaxed),0.0f,1.0f);
        juce::dsp::AudioBlock<float> block(buffer); auto sub=block.getSubBlock(0,size_t(n));
        juce::dsp::ProcessContextReplacing<float> context(sub);
        if(spec.type=="NAM" && model) {
            for(int i=0;i<n;++i) mono[size_t(i)]=0.5f*(buffer.getSample(0,i)+buffer.getSample(1,i));
            float* in[]{mono.data()}; float* out[]{modelOut.data()};
            if(resampler) resampler->ProcessBlock(in,out,n,modelFunction); else modelFunction(in,out,n);
            for(int c=0;c<2;++c) buffer.copyFrom(c,0,modelOut.data(),n);
            buffer.applyGain(0,n,juce::Decibels::decibelsToGain((p[0]-.5f)*24.0f));
        } else if(spec.type=="Cabinet IR" && spec.file.isNotEmpty()) convolution.process(context);
        else if(spec.type=="Compressor") {
            compressor.setThreshold(-48+48*p[0]); compressor.setRatio(1+19*p[1]);
            compressor.setAttack(1+49*p[2]); compressor.setRelease(20+480*p[3]); compressor.process(context);
        } else if(spec.type=="Chorus") {
            chorus.setRate(.1f+4.9f*p[0]); chorus.setDepth(p[1]); chorus.setCentreDelay(7);
            chorus.setFeedback(p[2]*.6f); chorus.setMix(p[3]); chorus.process(context);
        } else if(spec.type=="Reverb") {
            juce::Reverb::Parameters rp; rp.roomSize=p[0];rp.damping=p[1];rp.wetLevel=p[2]*.6f;rp.dryLevel=1-p[2]*.4f;rp.width=p[3];
            reverb.setParameters(rp); reverb.processStereo(buffer.getWritePointer(0),buffer.getWritePointer(1),n);
        } else if(spec.type=="EQ") {
            using C=juce::dsp::IIR::ArrayCoefficients<float>;
            eq[0].a=C::makeLowShelf(sr,120,.707f,juce::Decibels::decibelsToGain((p[0]-.5f)*24));
            eq[1].a=C::makePeakFilter(sr,750,.8f,juce::Decibels::decibelsToGain((p[1]-.5f)*24));
            eq[2].a=C::makeHighShelf(sr,std::min(4000.0,sr*.4),.707f,juce::Decibels::decibelsToGain((p[2]-.5f)*24));
            for(int c=0;c<2;++c) for(int i=0;i<n;++i) {
                auto x=buffer.getSample(c,i);for(auto& filter:eq)x=filter.tick(x,c);buffer.setSample(c,i,x);
            }
        } else if(spec.type=="Delay") {
            const auto samples=float(sr)*(.02f+.98f*p[0]);
            for(int i=0;i<n;++i) for(int c=0;c<2;++c) {
                const auto x=buffer.getSample(c,i), wet=delay.popSample(c,samples);
                delay.pushSample(c,x+wet*p[1]*.85f); buffer.setSample(c,i,x*(1-p[2]) + wet*p[2]);
            }
        } else if(spec.type=="Gate") {
            const auto threshold=juce::Decibels::decibelsToGain(-80.0f+p[0]*70);
            const auto release=float(std::exp(-1/(sr*(.02+.48*p[1]))));
            for(int c=0;c<2;++c) for(int i=0;i<n;++i) {
                auto x=buffer.getSample(c,i); envelope[c]=std::max(std::abs(x),envelope[c]*release);
                auto target=envelope[c]>threshold?1.0f:0.0f; gateGain[c]+=(target-gateGain[c])*(target>gateGain[c]?.02f:float(1-release));
                buffer.setSample(c,i,x*gateGain[c]);
            }
        } else if(spec.type=="Drive") {
            const auto drive=1+39*p[0], alpha=float(1-std::exp(-2*juce::MathConstants<double>::pi*(500+9500*p[1])/sr));
            for(int c=0;c<2;++c) for(int i=0;i<n;++i) {
                auto wet=std::tanh(buffer.getSample(c,i)*drive)/std::tanh(drive);
                toneState[c]+=alpha*(wet-toneState[c]); buffer.setSample(c,i,toneState[c]*juce::Decibels::decibelsToGain((p[2]-.5f)*24));
            }
        } else if(spec.type=="Gain") buffer.applyGain(0,n,juce::Decibels::decibelsToGain((p[0]-.5f)*48));
    }
};
}
juce::var StageSpec::toVar() const {
    auto* o=new juce::DynamicObject; o->setProperty("id",id);o->setProperty("type",type);o->setProperty("file",file);o->setProperty("title",title);
    if(file.isNotEmpty()) {
        auto f=juce::File(file);auto hash=f.getFileNameWithoutExtension();
        if(hash.length()==64 && hash.containsOnly("0123456789abcdef")){o->setProperty("assetId",hash);o->setProperty("assetExtension",f.getFileExtension());}
    }
    o->setProperty("enabled",controls->enabled.load());juce::Array<juce::var> p;for(auto& v:controls->values)p.add(v.load());o->setProperty("parameters",p);return juce::var(o);
}
StageSpec StageSpec::fromVar(const juce::var& v) {
    StageSpec s;s.id=v.getProperty("id",juce::Uuid().toString()).toString();s.type=v.getProperty("type","Gain").toString();
    s.file=v.getProperty("file","").toString();s.title=v.getProperty("title",s.type).toString();s.controls->enabled.store(bool(v.getProperty("enabled",true)));
    auto asset=v.getProperty("assetId","").toString(),ext=v.getProperty("assetExtension","").toString();
    if(asset.length()==64 && asset.containsOnly("0123456789abcdef") && (ext==".nam" || ext==".wav"))s.file=defaultDataDirectory().getChildFile("models").getChildFile(asset+ext).getFullPathName();
    auto p=v.getProperty("parameters",{});if(auto* a=p.getArray())for(int i=0;i<juce::jmin(4,a->size());++i)s.controls->values[size_t(i)].store(juce::jlimit(0.0f,1.0f,float((*a)[i])));
    return s;
}
struct Engine::Graph {
    std::vector<std::unique_ptr<Node>> nodes;
    juce::AudioBuffer<float> scratch{2,chunkSize};int latency=0;
    Graph(const std::vector<StageSpec>& specs,double sampleRate) {
        for(auto& s:specs) {
            if(!effectTypes.contains(s.type)) throw std::runtime_error("Unknown effect type");
            nodes.push_back(std::make_unique<Node>(s,sampleRate));latency+=nodes.back()->latency;
        }
    }
    void render(const juce::AudioBuffer<float>& in,int offset,int n) {
        scratch.copyFrom(0,0,in,0,offset,n);scratch.copyFrom(1,0,in,1,offset,n);
        for(auto& node:nodes)node->process(scratch,n);
    }
};
Engine::Engine() { rebuild(); }
Engine::~Engine() { ++generation;workers.removeAllJobs(true,-1); }
void Engine::prepare(double sampleRate) {
    // Hosts stop callbacks before prepareToPlay: never render a previous-rate
    // model while a new-rate graph is warming up in the background.
    {std::lock_guard lock(ownershipMutex);++generation;published.store(nullptr);active=incoming=nullptr;hazardA.store(nullptr);hazardB.store(nullptr);latencySamples.store(0);}
    rate.store(sampleRate>0?sampleRate:48000);rebuild();
}
void Engine::setChain(std::vector<StageSpec> specs) {
    if(specs.size()>16)throw std::runtime_error("Maximum 16 effects per chain");
    {std::lock_guard lock(controlMutex);specifications=std::move(specs);}rebuild();
}
std::vector<StageSpec> Engine::chain() const { std::lock_guard lock(controlMutex);return specifications; }
juce::String Engine::status() const {std::lock_guard lock(controlMutex);return currentStatus;}
void Engine::collect() {
    // Audio hands an incoming graph from B to A, then clears B. Read B first
    // so that this handoff cannot disappear between the two hazard reads.
    auto* p=published.load();auto* b=hazardB.load();auto* a=hazardA.load();
    std::erase_if(owned,[&](const auto& g){return g.get()!=p && g.get()!=a && g.get()!=b;});
}
void Engine::rebuild() {
    auto ticket=++generation;auto specs=chain();auto sr=rate.load();
    {std::lock_guard lock(controlMutex);currentStatus="Preparing audio chain...";}
    workers.addJob([this,ticket,specs=std::move(specs),sr] {
        try {
            if(ticket!=generation.load())return;
            auto graph=std::make_unique<Graph>(specs,sr);auto* ptr=graph.get();
            std::lock_guard lock(ownershipMutex);
            if(ticket!=generation.load())return;
            owned.push_back(std::move(graph));published.store(ptr);latencySamples.store(ptr->latency);collect();
            {std::lock_guard statusLock(controlMutex);currentStatus="Ready";}
        }catch(const std::exception& e){if(ticket==generation.load()){std::lock_guard lock(controlMutex);currentStatus="Load failed: "+juce::String(e.what());}}
    });
}
bool Engine::waitForBuild(int milliseconds) {
    auto deadline=juce::Time::getMillisecondCounter()+juce::uint32(milliseconds);
    while(workers.getNumJobs()>0 && juce::Time::getMillisecondCounter()<deadline)juce::Thread::sleep(2);
    return workers.getNumJobs()==0 && status()=="Ready";
}
void Engine::process(juce::AudioBuffer<float>& buffer) noexcept {
    if(buffer.getNumChannels()<2)return;
    if(!incoming) {
        Graph* candidate;
        do{candidate=published.load();hazardB.store(candidate);}while(candidate!=published.load());
        if(!active){active=candidate;hazardA.store(active);hazardB.store(nullptr);}
        else if(candidate!=active){incoming=candidate;fadePosition=0;}
        else hazardB.store(nullptr);
    }
    if(!active)return;
    try {
        for(int offset=0;offset<buffer.getNumSamples();offset+=chunkSize) {
            int n=juce::jmin(chunkSize,buffer.getNumSamples()-offset);active->render(buffer,offset,n);
            if(incoming)incoming->render(buffer,offset,n);
            for(int i=0;i<n;++i) {
                auto mix=incoming?juce::jlimit(0.0f,1.0f,float(fadePosition++)/fadeSamples):0.0f;
                for(int c=0;c<2;++c) {
                    auto x=active->scratch.getSample(c,i)*(1-mix)+(incoming?incoming->scratch.getSample(c,i)*mix:0);
                    if(!std::isfinite(x)){x=0;++nonFiniteSamples;}buffer.setSample(c,offset+i,x);
                }
            }
            if(incoming && fadePosition>=fadeSamples){active=incoming;hazardA.store(active);incoming=nullptr;hazardB.store(nullptr);}
        }
    }catch(...){buffer.clear();++nonFiniteSamples;}
}
}
