#pragma once
// The modular rack: an ordered list of modules (the amp is one of them) that replaces the fixed signal chain
// (docs/MODULAR_RACK.md). Framework-free, like Chain. Instances are built by the message thread and handed to the audio thread through
// atomic pointers; the audio thread never allocates.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
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

// What each type of module does with the channels, without building one (the editor uses it to say where a mono module sums a stereo pair).
Channels moduleChannels(ModuleType type);

// A compressor or overdrive right in front of the amp is run by the amp's own front end (as it always was), not by the rack.
enum class Fusion { None, Compressor, Overdrive };

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
    virtual Fusion fusion() const { return Fusion::None; }
    virtual bool isAmp() const { return false; }
    // Only for the amp: the rack tells it, each block, which pedals it must run itself and whether another pedal loads its input.
    virtual void setFrontEnd(Module* /*compressor*/, Module* /*overdrive*/, bool /*pedalBefore*/) {}
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
    std::uint64_t gen = 0;  // set by Rack::publish: which order this is
};

// How a new order takes effect. Duck: the output fades out (8 ms), the order changes at silence and fades back in, so moving a module
// while playing does not click; the modules keep their state (a reverb keeps its tail). Immediate: at the next block (state loads).
enum class Transition { Immediate, Duck };

class Rack {
public:
    static constexpr int kMaxInstances = 4;

    Rack();
    ~Rack();
    void prepare(double fs, int maxBlock);

    // Message thread. The module is prepared here. A module that is replaced or removed is not freed while the audio thread may still be inside it
    // or still run it (an old order is faded out first): it waits until the audio thread has finished a block, and applied the order that no longer
    // holds it, and a later call (collect) frees it. removeModule expects the order without the module to have been published first.
    void setModule(SlotRef slot, std::unique_ptr<Module> module);
    void removeModule(SlotRef slot);
    void collect();
    Module* module(SlotRef slot) const;  // message thread: the module the slot holds now
    Module* audioModule(SlotRef slot) const { return valid(slot) ? cell(slot).load() : nullptr; }  // audio thread: what a running order sees
    // Message thread: the order the next block will run. Entries without a module are skipped.
    void publish(const RackOrder& order, Transition how = Transition::Immediate);
    RackOrder order() const;

    // Audio thread. `r` is a buffer of at least n floats the rack may use for the right channel. Returns true when the result is a stereo pair
    // (l, r); false when it is mono (r is meaningless: copy l).
    bool process(float* l, float* r, std::size_t n);
private:
    bool run(const RackOrder* order, float* l, float* r, std::size_t n);
public:
    // Sum of the latency of the active modules in the current order.
    double latencySamples() const;
    // What the module used of the time of its blocks, in percent (a running average): 0 when it does not run. The audio thread writes, any thread reads.
    float moduleCpu(SlotRef slot) const { return valid(slot) ? cpu_[std::size_t(slot.type)][std::size_t(slot.instance)].load() : 0.0f; }
    void reset();

private:
    std::atomic<float>& cpuOf(SlotRef s) { return cpu_[std::size_t(s.type)][std::size_t(s.instance)]; }
    std::atomic<Module*>& cell(SlotRef s) { return table_[std::size_t(s.type)][std::size_t(s.instance)]; }
    const std::atomic<Module*>& cell(SlotRef s) const { return table_[std::size_t(s.type)][std::size_t(s.instance)]; }
    static bool valid(SlotRef s) { return int(s.type) >= 0 && int(s.type) < kModuleTypeCount && s.instance >= 0 && s.instance < kMaxInstances; }

    struct Retired {
        std::unique_ptr<Module> module;
        SlotRef slot;
        std::uint64_t needGen, needBlocks;
    };
    // Which pedals the amp runs itself (the compressor and the overdrive right in front of it, by position) and whether another pedal loads it.
    struct Fusions {
        int amp = -1;
        Module *comp = nullptr, *overdrive = nullptr;
        bool pedalBefore = false;
        bool fused[RackOrder::kMax] = {};
    };
    Fusions scanFusion(const RackOrder& order) const;
    Module* at(const RackOrder& order, int k) const;
    void retire(SlotRef slot, std::unique_ptr<Module> module, std::uint64_t needGen);

    std::array<std::array<std::atomic<Module*>, kMaxInstances>, kModuleTypeCount> table_;
    std::array<std::array<std::unique_ptr<Module>, kMaxInstances>, kModuleTypeCount> owned_;
    std::vector<Retired> retired_;
    std::uint64_t gen_ = 0;
    std::atomic<std::uint64_t> appliedGen_{0}, blocks_{0};  // audio thread: the order it has applied, and how many blocks it has finished
    std::array<RackOrder, 4> orders_{};  // a ring: the audio thread reads one pointer per block
    std::size_t nextOrder_ = 0;
    std::atomic<const RackOrder*> current_{nullptr};
    std::atomic<bool> duck_{false};
    std::array<std::array<std::atomic<float>, kMaxInstances>, kModuleTypeCount> cpu_{};
    // Audio thread: the order in force and the fade in progress.
    const RackOrder* applied_ = nullptr;
    enum class Fade { Idle, Out, In };
    Fade fade_ = Fade::Idle;
    int fadeLeft_ = 0, fadeLen_ = 384;
    double fs_ = 48000.0;
    int maxBlock_ = 512;
};

}  // namespace sas
