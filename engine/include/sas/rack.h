#pragma once
// The modular rack: an ordered list of modules (the amp is one of them) that replaces the fixed signal chain
// (docs/MODULAR_RACK.md). Framework-free, like Chain. Instances are built by the message thread and handed to the audio thread through
// atomic pointers; the audio thread never allocates.

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace sas {

// Append-only: a saved project stores the names, not the numbers, but keep the order anyway.
enum class ModuleType { Amp = 0, Cabinet, Gate, Compressor, Overdrive, NamPedal, Phaser, Chorus, Vibe, Pitch, Eq, Delay, Reverb };
constexpr int kModuleTypeCount = int(ModuleType::Reverb) + 1;

const char* moduleTypeName(ModuleType t);
std::optional<ModuleType> moduleTypeFromName(std::string_view name);

// What a module does with the two channels. The rack makes the signal fit:
//  MonoOnly     sums a stereo pair to mono first, then runs on the left channel (r == nullptr).
//  MonoToStereo sums a stereo pair, then runs with a right buffer it must fill (a copy of l when it adds no width); the result is stereo.
//  Stereo       needs a pair: a mono signal is copied into the right buffer first.
//  Flexible     takes whatever comes: r is nullptr while the signal is mono (the phaser, chorus and vibe have always run that way).
enum class Channels { MonoOnly, MonoToStereo, Stereo, Flexible };

class Module {
public:
    virtual ~Module() = default;
    virtual void prepare(double fs, int maxBlock) = 0;
    virtual void process(float* l, float* r, std::size_t n) = 0;
    virtual double latencySamples() const { return 0.0; }
    // False = switched off and nothing left to ring out: the rack skips it (it costs nothing).
    virtual bool active() const = 0;
    // True for pedals that load the guitar (compressor, TS): in front of the amp they replace its source impedance.
    virtual bool buffered() const { return false; }
    virtual Channels channels() const = 0;
    virtual void reset() = 0;
};

struct SlotRef {
    ModuleType type = ModuleType::Amp;
    int instance = 0;
    bool operator==(const SlotRef&) const = default;
};

struct RackOrder {
    static constexpr int kMax = 13;  // twelve modules and the amp
    std::array<SlotRef, kMax> slots{};
    int count = 0;
};

class Rack {
public:
    static constexpr int kMaxInstances = 4;

    Rack();
    ~Rack();
    void prepare(double fs, int maxBlock);

    // Message thread. The module is prepared here; the previous one in the slot is kept until the next call (the audio thread may be inside it).
    void setModule(SlotRef slot, std::unique_ptr<Module> module);
    void removeModule(SlotRef slot);
    Module* module(SlotRef slot) const;
    // Message thread: the order the next block will run. Entries without a module are skipped.
    void publish(const RackOrder& order);
    RackOrder order() const;

    // Audio thread. `r` is a buffer of at least n floats the rack may use for the right channel. Returns true when the result is a stereo pair
    // (l, r); false when it is mono (r is meaningless: copy l).
    bool process(float* l, float* r, std::size_t n);
    // Sum of the latency of the active modules in the current order.
    double latencySamples() const;
    void reset();

private:
    std::atomic<Module*>& cell(SlotRef s) { return table_[std::size_t(s.type)][std::size_t(s.instance)]; }
    const std::atomic<Module*>& cell(SlotRef s) const { return table_[std::size_t(s.type)][std::size_t(s.instance)]; }
    static bool valid(SlotRef s) { return int(s.type) >= 0 && int(s.type) < kModuleTypeCount && s.instance >= 0 && s.instance < kMaxInstances; }

    std::array<std::array<std::atomic<Module*>, kMaxInstances>, kModuleTypeCount> table_;
    std::vector<std::unique_ptr<Module>> owned_, retired_;
    std::array<RackOrder, 4> orders_{};  // a ring: the audio thread reads one pointer per block
    std::size_t nextOrder_ = 0;
    std::atomic<const RackOrder*> current_{nullptr};
    double fs_ = 48000.0;
    int maxBlock_ = 512;
};

}  // namespace sas
