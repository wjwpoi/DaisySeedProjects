#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <mutex>

namespace tonedock {
inline const juce::StringArray effectTypes {"Gate", "Compressor", "Drive", "NAM", "Cabinet IR", "EQ", "Chorus", "Delay", "Reverb", "Gain"};
struct Controls {
    std::atomic<bool> enabled{true};
    std::array<std::atomic<float>,4> values{{0.5f,0.5f,0.5f,0.5f}};
};
struct StageSpec {
    juce::String id, type, file, title;
    std::shared_ptr<Controls> controls = std::make_shared<Controls>();
    juce::var toVar() const;
    static StageSpec fromVar(const juce::var&);
};
class Engine {
public:
    Engine();
    ~Engine();
    void prepare(double sampleRate);
    void setChain(std::vector<StageSpec>);
    std::vector<StageSpec> chain() const;
    void process(juce::AudioBuffer<float>&) noexcept;
    juce::String status() const;
    int latency() const { return latencySamples.load(); }
    bool waitForBuild(int milliseconds = 20000);
    std::atomic<uint64_t> nonFiniteSamples{0};
private:
    struct Graph;
    void rebuild();
    void collect();
    mutable std::mutex controlMutex, ownershipMutex;
    std::vector<StageSpec> specifications;
    std::vector<std::unique_ptr<Graph>> owned;
    juce::String currentStatus{"Ready"};
    std::atomic<Graph*> published{nullptr}, hazardA{nullptr}, hazardB{nullptr};
    Graph* active = nullptr;
    Graph* incoming = nullptr;
    int fadePosition = 0;
    std::atomic<int> generation{0}, latencySamples{0};
    std::atomic<double> rate{48000.0};
    juce::ThreadPool workers{1};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Engine)
};
}
