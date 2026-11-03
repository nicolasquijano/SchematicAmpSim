#pragma once
// A Neural Amp Modeler model (.nam file, or an impulse response .wav) run at the host rate. Wraps the NAM core so nothing outside
// engine/src/nam_model.cpp needs its headers. Loading allocates and may take a while: do it off the audio thread and hand the finished
// model over; process() does not allocate.
#include <cstddef>
#include <memory>
#include <string>

namespace sas {

class NamModel {
public:
    ~NamModel();
    NamModel(const NamModel&) = delete;
    NamModel& operator=(const NamModel&) = delete;

    // Reads the file and prepares it for `fs` and blocks of up to `maxBlock` samples. Returns null and sets `error` when the file cannot
    // be read or is not a model this build supports.
    static std::shared_ptr<NamModel> load(const std::string& path, double fs, int maxBlock, std::string& error);

    // The same from the bytes of a .nam file (what a project keeps inside its state); `name` is what name() will say.
    static std::shared_ptr<NamModel> loadFromMemory(const void* data, std::size_t size, const std::string& name, double fs, int maxBlock,
                                                    std::string& error);

    // Mono in, mono out; `n` may be any size. Not thread-safe: one audio thread at a time.
    void process(const float* in, float* out, std::size_t n);
    // Clears the model's memory (after a long silence or a transport jump).
    void reset();

    const std::string& path() const;
    // The model's name (its file name without the extension).
    const std::string& name() const;
    // The rate the model was trained at (0 when the file does not say) and whether it runs at the host rate as it is: a model that is
    // not rate-flexible and was trained at another rate sounds higher or lower than it should.
    double trainedRate() const;
    bool rateMatches() const;
    // The model's own loudness in dB (what it gives for a typical input), when the file stores it.
    bool hasLoudness() const;
    double loudnessDb() const;
    // Gain that brings the model's output to a common loudness (1 when it does not say).
    float normalizeGain() const;
    // The input level in dBu that is 0 dBFS, when stored (see the NAM docs); NaN otherwise.
    double inputLevelDbu() const;

private:
    NamModel();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace sas
