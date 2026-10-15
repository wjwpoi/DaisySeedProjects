#include "Library.h"
#include <NAM/get_dsp.h>
#include <stdexcept>
#include <mutex>

namespace tonedock {
namespace {
constexpr juce::int64 maxFileBytes = 64 * 1024 * 1024;
// POSIX fcntl locks are process-wide; also serialize threads in this process.
std::mutex libraryMutex;
void require(bool condition, const juce::String& message) {
    if (!condition) throw std::runtime_error(message.toStdString());
}
juce::String property(const juce::var& v, const char* name) { return v.getProperty(name, "").toString(); }
}
juce::File defaultDataDirectory() {
    auto overridePath=juce::SystemStats::getEnvironmentVariable("TONEDOCK_DATA_DIR",{});
    if(overridePath.isNotEmpty())return juce::File(overridePath);
    auto parent=juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    parent=parent.getChildFile("Application Support");
#endif
    return parent.getChildFile("ToneDock");
}
std::optional<int> parseToneLink(const juce::String& raw) {
    auto text = raw.trim();
    if (text.length() > 4096) return {};
    juce::URL url(text);
    auto scheme = url.getScheme().toLowerCase(), host = url.getDomain().toLowerCase();
    juce::String id;
    if (scheme == "tonedock" && host == "tone") id = url.getSubPath().trimCharactersAtStart("/");
    else if (scheme == "https" && (host == "tone3000.com" || host == "www.tone3000.com")) {
        auto pieces = juce::StringArray::fromTokens(url.getSubPath(), "/", "");
        if (pieces.size() != 2 || pieces[0] != "tones") return {};
        id = pieces[1].fromLastOccurrenceOf("-", false, false);
        if (id.isEmpty()) id = pieces[1];
    } else return {};
    if (id.isEmpty() || id.length() > 9 || !id.containsOnly("0123456789") || id.getIntValue() <= 0) return {};
    return id.getIntValue();
}
juce::var ModelEntry::toVar() const {
    auto* o = new juce::DynamicObject;
    for (auto&& field : {std::pair{"id", id}, {"name", name}, {"kind", kind}, {"category", category},
                        {"architecture", architecture}, {"author", author}, {"license", license},
                        {"sourceUrl", sourceUrl}, {"toneId", toneId}}) o->setProperty(field.first, field.second);
    juce::Array<juce::var> tagValues; for (const auto& tag : tags) tagValues.add(tag);
    o->setProperty("tags", tagValues); o->setProperty("sampleRate", sampleRate); o->setProperty("favorite", favorite);
    return juce::var(o);
}
ModelEntry ModelEntry::fromVar(const juce::var& v) {
    ModelEntry e;
    e.id = property(v,"id"); e.name = property(v,"name"); e.kind = property(v,"kind");
    e.category = property(v,"category"); e.architecture = property(v,"architecture"); e.author = property(v,"author");
    e.license = property(v,"license"); e.sourceUrl = property(v,"sourceUrl"); e.toneId = property(v,"toneId");
    e.favorite = bool(v.getProperty("favorite",false)); e.sampleRate = double(v.getProperty("sampleRate",0));
    if (auto* a = v.getProperty("tags",{}).getArray()) for (auto& tag : *a) e.tags.add(tag.toString());
    return e;
}
Library::Library(juce::File directory) : root(std::move(directory)) {
    require(root.getChildFile("models").createDirectory().wasOk(), "Cannot create model library");
}
juce::var Library::readIndex() const {
    auto index = root.getChildFile("library.json");
    if (!index.existsAsFile()) return juce::Array<juce::var>{};
    auto result = juce::JSON::parse(index);
    require(result.isArray(), "Library index is invalid; restore library.json before changing the library");
    return result;
}
void Library::writeIndex(const juce::var& value) const {
    juce::TemporaryFile temporary(root.getChildFile("library.json"));
    require(temporary.getFile().replaceWithText(juce::JSON::toString(value)), "Cannot save library index");
    require(temporary.overwriteTargetFileWithTemporary(), "Cannot commit library index");
}
std::vector<ModelEntry> Library::entries() const {
    std::lock_guard threadLock(libraryMutex);
    juce::InterProcessLock lock("ToneDock-" + juce::String(root.getFullPathName().hashCode64()));
    require(lock.enter(3000), "Library is busy in another application");
    struct Exit { juce::InterProcessLock& l; ~Exit(){l.exit();} } exit{lock};
    auto data = readIndex(); std::vector<ModelEntry> result;
    for (const auto& value : *data.getArray()) result.push_back(ModelEntry::fromVar(value));
    return result;
}
juce::File Library::fileFor(const ModelEntry& entry) const {
    require(entry.id.length() == 64 && entry.id.containsOnly("0123456789abcdef"), "Invalid model identifier");
    return root.getChildFile("models").getChildFile(entry.id + (entry.kind == "ir" ? ".wav" : ".nam"));
}
ModelEntry Library::importFile(const juce::File& file, const juce::var& provenance) {
    require(file.existsAsFile() && file.getSize() > 0 && file.getSize() <= maxFileBytes, "Model is missing, empty, or larger than 64 MiB");
    auto ext = file.getFileExtension().toLowerCase();
    require(ext == ".nam" || ext == ".wav", "Only .nam and .wav IR files are supported");
    juce::MemoryBlock bytes; require(file.loadFileAsData(bytes), "Cannot read model file");
    ModelEntry entry; entry.id = juce::SHA256(bytes).toHexString();
    entry.kind = ext == ".nam" ? "nam" : "ir";
    entry.name = file.getFileNameWithoutExtension(); entry.category = entry.kind == "ir" ? "Cabinet IR" : "Unsorted";
    if (entry.kind == "nam") {
        auto model = nam::get_dsp(std::filesystem::u8path(file.getFullPathName().toStdString()), nam::DspLoadOptions{false});
        require(model->NumInputChannels() == 1 && model->NumOutputChannels() == 1, "This version loads mono NAM captures; stereo/conditional captures are unsupported");
        entry.sampleRate = model->GetExpectedSampleRate();
        auto json = juce::JSON::parse(file);
        entry.architecture = property(json,"architecture");
        auto metadata = json.getProperty("metadata",{});
        if (property(metadata,"name").isNotEmpty()) entry.name = property(metadata,"name");
        entry.author = property(metadata,"modeled_by");
        if (entry.author.isEmpty()) entry.author = property(metadata,"author");
        entry.license = property(metadata,"license");
        auto hint = (entry.name + " " + property(metadata,"gear_type")).toLowerCase();
        if (hint.contains("amp")) entry.category = "Amplifier";
        else if (hint.contains("pedal") || hint.contains("drive")) entry.category = "Pedal";
    } else {
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        require(reader != nullptr && reader->numChannels <= 2 && reader->lengthInSamples > 0 && reader->lengthInSamples <= reader->sampleRate * 2,
                "IR must be mono/stereo WAV and at most two seconds");
        entry.sampleRate = reader->sampleRate; entry.architecture = "Convolution";
    }
    for (auto&& [key, target] : {std::pair{"author", &entry.author}, {"license", &entry.license},
        {"sourceUrl", &entry.sourceUrl}, {"toneId", &entry.toneId}, {"category", &entry.category}, {"name", &entry.name}}) {
        auto value = property(provenance,key); if (value.isNotEmpty()) *target = value;
    }
    if (auto* values = provenance.getProperty("tags",{}).getArray()) for (auto& tag : *values) entry.tags.addIfNotAlreadyThere(tag.toString());
    std::lock_guard threadLock(libraryMutex);
    juce::InterProcessLock lock("ToneDock-" + juce::String(root.getFullPathName().hashCode64()));
    require(lock.enter(3000), "Library is busy");
    struct Exit { juce::InterProcessLock& l; ~Exit(){l.exit();} } exit{lock};
    auto index = readIndex();
    for (auto& v : *index.getArray()) if (property(v,"id") == entry.id) return ModelEntry::fromVar(v);
    juce::TemporaryFile temporary(fileFor(entry));
    require(temporary.getFile().replaceWithData(bytes.getData(),bytes.getSize()) && temporary.overwriteTargetFileWithTemporary(), "Cannot copy model into library");
    index.getArray()->add(entry.toVar()); writeIndex(index); return entry;
}
std::vector<ModelEntry> Library::importArchive(const juce::File& file) {
    require(file.getSize() <= maxFileBytes, "Archive larger than 64 MiB");
    juce::ZipFile zip(file); require(zip.getNumEntries() > 0 && zip.getNumEntries() <= 256, "Archive is empty or has too many files");
    juce::int64 total = 0; std::vector<ModelEntry> imported;
    auto staging = root.getChildFile("staging").getChildFile(juce::Uuid().toString());
    require(staging.createDirectory().wasOk(), "Cannot create import staging directory");
    struct Clean { juce::File f; ~Clean(){ f.deleteRecursively(); } } clean{staging};
    for (int i = 0; i < zip.getNumEntries(); ++i) {
        auto* item = zip.getEntry(i); auto name = item->filename.replaceCharacter('\\','/');
        require(!name.startsWithChar('/') && !name.contains(":") && !juce::StringArray::fromTokens(name,"/","").contains(".."), "Unsafe ZIP entry path");
        total += item->uncompressedSize; require(total <= 128 * 1024 * 1024 && item->uncompressedSize <= maxFileBytes, "Expanded archive exceeds size limit");
        if (!(name.endsWithIgnoreCase(".nam") || name.endsWithIgnoreCase(".wav"))) continue;
        std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(i)); require(in != nullptr, "Cannot read ZIP entry");
        auto target = staging.getChildFile(juce::String(i) + "-" + juce::File(name).getFileName());
        juce::MemoryBlock data; in->readIntoMemoryBlock(data, int(maxFileBytes + 1));
        require(juce::int64(data.getSize()) == item->uncompressedSize && target.replaceWithData(data.getData(),data.getSize()), "Incomplete ZIP entry");
        imported.push_back(importFile(target));
    }
    require(!imported.empty(), "Archive contains no supported models or IRs"); return imported;
}
void Library::update(const ModelEntry& entry) {
    fileFor(entry);
    std::lock_guard threadLock(libraryMutex);
    juce::InterProcessLock lock("ToneDock-" + juce::String(root.getFullPathName().hashCode64()));
    require(lock.enter(3000), "Library is busy");
    struct Exit { juce::InterProcessLock& l; ~Exit(){l.exit();} } exit{lock};
    auto index = readIndex(); bool found = false;
    for (auto& value : *index.getArray()) if (property(value,"id") == entry.id) { value = entry.toVar(); found = true; break; }
    require(found, "Model no longer exists in library"); writeIndex(index);
}
void Library::remove(const juce::String& id) {
    std::lock_guard threadLock(libraryMutex);
    juce::InterProcessLock lock("ToneDock-" + juce::String(root.getFullPathName().hashCode64()));
    require(lock.enter(3000), "Library is busy");
    struct Exit { juce::InterProcessLock& l; ~Exit(){l.exit();} } exit{lock};
    auto index = readIndex();
    for (int i = index.size(); --i >= 0;) if (property(index[i],"id") == id) index.getArray()->remove(i);
    // Keep content-addressed bytes: saved DAW sessions may still reference them.
    writeIndex(index);
}
}
