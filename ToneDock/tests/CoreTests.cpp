#include "Processor.h"
#include "Editor.h"
#include "OAuth.h"
#include <NAM/get_dsp.h>
#include <atomic>
#include <cmath>
#include <iostream>
#include <thread>

namespace {
int checks=0;
void check(bool yes,const char* message){++checks;if(!yes)throw std::runtime_error(message);}
template<class F>void rejects(F fn,const char* message){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}check(failed,message);}
tonedock::StageSpec stage(const juce::String& type){tonedock::StageSpec s;s.id=juce::Uuid().toString();s.type=type;s.title=type;return s;}
void fill(juce::AudioBuffer<float>& b,float value){for(int c=0;c<b.getNumChannels();++c)for(int i=0;i<b.getNumSamples();++i)b.setSample(c,i,value);}
}
int main(){
    juce::ScopedJuceInitialiser_GUI initialise;
    auto directory=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ToneDock-test-"+juce::Uuid().toString());directory.createDirectory();
    struct Cleanup{juce::File f;~Cleanup(){f.deleteRecursively();}}cleanup{directory};
#if JUCE_WINDOWS
    _putenv_s("TONEDOCK_DATA_DIR",directory.getChildFile("shared").getFullPathName().toRawUTF8());
#else
    setenv("TONEDOCK_DATA_DIR",directory.getChildFile("shared").getFullPathName().toRawUTF8(),1);
#endif
    try {
        check(tonedock::parseToneLink("tonedock://tone/42")==42,"custom protocol");
        check(tonedock::parseToneLink("https://www.tone3000.com/tones/test-amp-42")==42,"tone page");
        for(auto link:juce::StringArray{"tonedock://tone/0","tonedock://tone/42/extra","https://evil.test/tones/42","javascript:alert(1)","https://www.tone3000.com.evil.test/tones/42","tonedock://tone/999999999999999"})check(!tonedock::parseToneLink(link),"reject invalid links");
        auto pkce=t3k::oauth::Pkce::generate(),other=t3k::oauth::Pkce::generate();
        check(pkce.codeVerifier.length()==43 && pkce.codeChallenge.length()==43,"PKCE lengths");
        check(pkce.state!=other.state && pkce.codeVerifier!=other.codeVerifier,"fresh OS random PKCE");
        auto hash=juce::SHA256(pkce.codeVerifier.toRawUTF8(),pkce.codeVerifier.getNumBytesAsUTF8()).getRawData();
        auto expected=juce::Base64::toBase64(hash.getData(),hash.getSize()).replaceCharacter('+','-').replaceCharacter('/','_').removeCharacters("=");
        check(pkce.codeChallenge==expected,"PKCE S256");
        auto parsed=t3k::oauth::Callback::parse("?code=abc%2B123&state=xyz&tone_id=42");check(parsed.code=="abc+123" && parsed.toneId=="42","OAuth callback parsing");
        tonedock::Library library(directory.getChildFile("library"));
        auto fixture=directory.getChildFile("Fixture Amp.nam");
        fixture.replaceWithText(R"({"version":"0.5.4","architecture":"Linear","config":{"receptive_field":1,"bias":false},"weights":[0.5],"sample_rate":48000,"metadata":{"name":"Fixture Amp","modeled_by":"Tests","license":"MIT"}})");
        auto entry=library.importFile(fixture);check(entry.category=="Amplifier" && entry.author=="Tests","automatic metadata classification");
        check(library.importFile(fixture).id==entry.id && library.entries().size()==1,"SHA256 deduplication");
        entry.tags={"Clean","Test"};entry.favorite=true;entry.category="Custom";library.update(entry);
        auto reopened=tonedock::Library(library.directory()).entries();check(reopened.size()==1 && reopened[0].tags.contains("Clean") && reopened[0].favorite,"persistent tags and favorite");
        auto invalid=directory.getChildFile("broken.nam");invalid.replaceWithText("{broken");rejects([&]{library.importFile(invalid);},"reject corrupt NAM");
        check(library.entries().size()==1,"invalid import preserves index");
        juce::ZipFile::Builder zip;zip.addFile(fixture,6,"amp.nam");auto archive=directory.getChildFile("models.zip");{auto out=archive.createOutputStream();check(zip.writeToStream(*out,nullptr),"write ZIP fixture");}
        check(library.importArchive(archive).size()==1,"ZIP import");
        juce::ZipFile::Builder unsafe;unsafe.addFile(fixture,6,"../outside.nam");auto badZip=directory.getChildFile("unsafe.zip");{auto out=badZip.createOutputStream();unsafe.writeToStream(*out,nullptr);}
        rejects([&]{library.importArchive(badZip);},"reject ZIP traversal");
        auto another=directory.getChildFile("Second.nam");another.replaceWithText(fixture.loadFileAsString().replace("[0.5]","[0.25]"));
        std::exception_ptr failureA,failureB;
        std::thread writerA([&]{try{library.importFile(another);}catch(...){failureA=std::current_exception();}});
        std::thread writerB([&]{try{tonedock::Library(library.directory()).update(entry);}catch(...){failureB=std::current_exception();}});
        writerA.join();writerB.join();check(!failureA && !failureB && library.entries().size()==2,"concurrent library writes preserve both changes");
        auto second=library.importFile(another);library.remove(second.id);
        library.remove(entry.id);check(library.entries().empty() && library.fileFor(entry).existsAsFile(),"library removal preserves preset asset");
        tonedock::Engine engine;auto gain=stage("Gain");gain.controls->values[0]=.5f;
        engine.setChain({gain});engine.prepare(48000);check(engine.waitForBuild(),"prepare chain");
        juce::AudioBuffer<float> audio(2,257);fill(audio,.2f);engine.process(audio);
        check(std::abs(audio.getSample(0,250)-.2f)<1e-6,"identity gain and odd buffer size");
        gain.controls->values[0]=.625f;fill(audio,.2f);engine.process(audio);check(std::abs(audio.getSample(0,30)-.2f*std::pow(10.0f,6.0f/20.0f))<1e-5,"live parameter update");
        for(auto type:tonedock::effectTypes){if(type=="NAM" || type=="Cabinet IR")continue;auto s=stage(type);engine.setChain({s});check(engine.waitForBuild(),"prepare each effect");
            for(int block=0;block<8;++block){fill(audio,.1f);engine.process(audio);for(int i=0;i<audio.getNumSamples();++i)check(std::isfinite(audio.getSample(0,i)),"finite effect output");}}
        for(auto name:juce::StringArray{"lstm.nam","wavenet.nam","A2.nam","wavenet_a1_standard.nam"}) {
            auto path=juce::File(TONEDOCK_NAM_FIXTURES).getChildFile(name);auto s=stage("NAM");s.file=path.getFullPathName();
            tonedock::Engine test;test.setChain({s});test.prepare(48000);check(test.waitForBuild(),"load full NAM architectures");
            auto reference=nam::get_dsp(std::filesystem::u8path(path.getFullPathName().toStdString()),nam::DspLoadOptions{false});reference->Reset(48000,384);
            std::array<float,64> in{},out{};juce::AudioBuffer<float> block(2,64);float* ip[]{in.data()};float* op[]{out.data()};
            double maximum=0,energy=0;
            for(int b=0;b<24;++b){for(int i=0;i<64;++i){in[size_t(i)]=.05f*std::sin(float(b*64+i)*.03f);block.setSample(0,i,in[size_t(i)]);block.setSample(1,i,in[size_t(i)]);}reference->process(ip,op,64);test.process(block);
                for(int i=0;i<64;++i){maximum=std::max(maximum,double(std::abs(block.getSample(0,i)-out[size_t(i)])));energy+=out[size_t(i)]*out[size_t(i)];check(std::isfinite(block.getSample(0,i)),"NAM finite");}}
            check(energy>0,"NAM reference is non-silent");
            check(maximum<1e-4,"NAM output agrees with upstream runtime");std::cout<<name<<" reference max error "<<maximum<<"\n";
        }
        auto resampledStage=stage("NAM");resampledStage.file=juce::File(TONEDOCK_NAM_FIXTURES).getChildFile("wavenet.nam").getFullPathName();
        for(auto sr:{44100.0,96000.0}){tonedock::Engine resampled;resampled.setChain({resampledStage});resampled.prepare(sr);check(resampled.waitForBuild() && resampled.latency()>0,"sample-rate conversion and latency");
            for(int n:{1,17,64,257,511}){juce::AudioBuffer<float> block(2,n);fill(block,.02f);for(int b=0;b<16;++b){resampled.process(block);for(int i=0;i<n;++i)check(std::isfinite(block.getSample(0,i)),"resampling variable buffers");}}}
        std::atomic<bool> running{true};std::thread render([&]{juce::AudioBuffer<float> block(2,64);while(running){fill(block,.01f);engine.process(block);}});
        for(int i=0;i<60;++i){auto s=stage(i%2?"Gain":"EQ");engine.setChain({s});if(i%5==0)juce::Thread::sleep(2);}
        auto editsFinished=engine.waitForBuild();running=false;render.join();check(editsFinished,"concurrent chain edits");check(engine.nonFiniteSamples.load()==0,"no render faults during edits");
        tonedock::Processor processor;processor.prepareToPlay(48000,128);processor.engine.setChain({gain});check(processor.engine.waitForBuild(),"processor graph");
        auto shared=processor.services->library.importFile(fixture);processor.loadModel(shared);check(processor.engine.waitForBuild(),"load shared library model");
        juce::MemoryBlock state;processor.getStateInformation(state);processor.engine.setChain({});processor.setStateInformation(state.getData(),int(state.getSize()));check(processor.engine.chain().size()==2,"DAW state restoration");
        auto restored=processor.engine.chain().back();check(juce::File(restored.file).existsAsFile(),"portable model asset resolution");
        auto before=processor.engine.chain().size();processor.setStateInformation("bad",3);check(processor.engine.chain().size()==before,"invalid preset keeps chain");
        check(processor.engine.waitForBuild(),"restored processor graph ready");
        juce::AudioBuffer<float> hostAudio(2,128);juce::MidiBuffer midi;fill(hostAudio,.2f);processor.processBlock(hostAudio,midi);
        auto expectedHost=.2f*juce::Decibels::decibelsToGain(6.0f)*.5f*juce::Decibels::decibelsToGain(-12.0f);
        check(std::abs(hostAudio.getSample(0,64)-expectedHost)<1e-5,"complete plugin input-to-output signal path");
        processor.getBypassParameter()->setValueNotifyingHost(1);fill(hostAudio,.2f);processor.processBlock(hostAudio,midi);
        check(std::abs(hostAudio.getSample(0,64)-.2f)<1e-6,"host bypass preserves dry signal");
        {
            tonedock::Processor guiProcessor;guiProcessor.loadModel(shared);check(guiProcessor.engine.waitForBuild(),"GUI fixture model loads");
            std::unique_ptr<tonedock::Editor> editor(static_cast<tonedock::Editor*>(guiProcessor.createEditorIfNeeded()));
            auto imagePath=juce::SystemStats::getEnvironmentVariable("TONEDOCK_SCREENSHOT",{});
            if(imagePath.isNotEmpty()) {
                auto target=juce::File(imagePath);target.getParentDirectory().createDirectory();
                auto image=editor->createComponentSnapshot(editor->getLocalBounds());auto stream=target.createOutputStream();
                check(stream!=nullptr,"create screenshot file");stream->setPosition(0);stream->truncate();
                check(juce::PNGImageFormat().writeImageToStream(image,*stream),"render actual native editor screenshot");
            }
            auto click=[&](const juce::String& name){for(int i=0;i<editor->getNumChildComponents();++i)if(auto* button=dynamic_cast<juce::TextButton*>(editor->getChildComponent(i));button && button->getButtonText()==name){button->onClick();return;}throw std::runtime_error("UI button missing");};
            auto selected=guiProcessor.engine.chain()[1];
            for(int i=0;i<editor->getNumChildComponents();++i)if(auto* slider=dynamic_cast<juce::Slider*>(editor->getChildComponent(i));slider && slider->getSliderStyle()==juce::Slider::RotaryHorizontalVerticalDrag){slider->setValue(.7,juce::sendNotificationSync);break;}
            check(std::abs(selected.controls->values[0].load()-.7f)<.001,"UI knob controls audio parameter");
            click("+ Add");check(guiProcessor.engine.chain().size()==5,"UI adds selected effect");
            click("Up");check(guiProcessor.engine.chain()[0].id==selected.id,"UI moves effect up");
            click("Enable / bypass");check(!selected.controls->enabled.load(),"UI bypasses effect");
            juce::DragAndDropTarget::SourceDetails drag(juce::var("stage:"+selected.id),editor.get(),juce::Point<int>(500,420));
            editor->itemDropped(drag);check(guiProcessor.engine.chain()[3].id==selected.id,"drag reorders actual effect chain");
            click("Delete");auto edited=guiProcessor.engine.chain();check(edited.size()==4 && std::none_of(edited.begin(),edited.end(),[&](const auto& s){return s.id==selected.id;}),"UI removes effect");
        }
        std::cout<<"PASS "<<checks<<" checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<"\n";return 1;}
}
