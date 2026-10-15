#pragma once
#include <JuceHeader.h>
#include <optional>

namespace tonedock {
juce::File defaultDataDirectory();
std::optional<int> parseToneLink(const juce::String&);
struct ModelEntry {
    juce::String id, name, kind, category, architecture, author, license, sourceUrl, toneId;
    juce::StringArray tags;
    double sampleRate = 0;
    bool favorite = false;
    juce::var toVar() const;
    static ModelEntry fromVar(const juce::var&);
};
class Library {
public:
    explicit Library(juce::File directory = defaultDataDirectory());
    std::vector<ModelEntry> entries() const;
    ModelEntry importFile(const juce::File&, const juce::var& provenance = {});
    std::vector<ModelEntry> importArchive(const juce::File&);
    void update(const ModelEntry&);
    void remove(const juce::String& id);
    juce::File fileFor(const ModelEntry&) const;
    juce::File directory() const { return root; }
private:
    juce::File root;
    juce::var readIndex() const;
    void writeIndex(const juce::var&) const;
};
}
