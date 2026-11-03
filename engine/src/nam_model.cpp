#include "sas/nam_model.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

namespace sas {

namespace {
// The loudness every model is brought to (dB): about what a normally-driven amp model gives, so models from different captures sit close.
constexpr double kTargetLoudnessDb = -18.0;
}  // namespace

struct NamModel::Impl {
    std::unique_ptr<nam::DSP> dsp;
    std::string path, name;
    double fs = 48000.0;
    int maxBlock = 512;
};

NamModel::NamModel() : impl_(std::make_unique<Impl>()) {}
NamModel::~NamModel() = default;

std::shared_ptr<NamModel> NamModel::load(const std::string& path, double fs, int maxBlock, std::string& error) {
    error.clear();
    try {
        const std::filesystem::path p{std::u8string(reinterpret_cast<const char8_t*>(path.c_str()))};
        if (!std::filesystem::exists(p)) {
            error = "the file does not exist";
            return nullptr;
        }
        // Do the (expensive) warm-up once, here, rather than on the audio thread when the model is first used.
        auto dsp = nam::get_dsp(p);
        if (!dsp) {
            error = "the file is not a model";
            return nullptr;
        }
        if (dsp->NumInputChannels() != 1) {
            error = "only models with one input are supported";
            return nullptr;
        }
        auto m = std::shared_ptr<NamModel>(new NamModel());
        m->impl_->dsp = std::move(dsp);
        m->impl_->path = path;
        const auto stem = p.stem().u8string();
        m->impl_->name.assign(stem.begin(), stem.end());
        m->impl_->fs = fs;
        m->impl_->maxBlock = std::max(1, maxBlock);
        m->impl_->dsp->Reset(fs, m->impl_->maxBlock);
        return m;
    } catch (const std::exception& e) {
        error = e.what();
    } catch (...) {
        error = "the file could not be read";
    }
    return nullptr;
}

std::shared_ptr<NamModel> NamModel::loadFromMemory(const void* data, std::size_t size, const std::string& name, double fs, int maxBlock,
                                                   std::string& error) {
    error.clear();
    try {
        const auto json = nlohmann::json::parse(static_cast<const char*>(data), static_cast<const char*>(data) + size);
        auto dsp = nam::get_dsp(json);
        if (!dsp) {
            error = "the file is not a model";
            return nullptr;
        }
        if (dsp->NumInputChannels() != 1) {
            error = "only models with one input are supported";
            return nullptr;
        }
        auto m = std::shared_ptr<NamModel>(new NamModel());
        m->impl_->dsp = std::move(dsp);
        m->impl_->name = name;
        m->impl_->fs = fs;
        m->impl_->maxBlock = std::max(1, maxBlock);
        m->impl_->dsp->Reset(fs, m->impl_->maxBlock);
        return m;
    } catch (const std::exception& e) {
        error = e.what();
    } catch (...) {
        error = "the file could not be read";
    }
    return nullptr;
}

void NamModel::process(const float* in, float* out, std::size_t n) {
    auto& d = *impl_->dsp;
    const bool stereoOut = d.NumOutputChannels() > 1;
    std::size_t done = 0;
    while (done < n) {
        const std::size_t m = std::min<std::size_t>(n - done, std::size_t(impl_->maxBlock));
        // The core reads and writes through channel pointers; mono models use the same buffers in place.
        float* inPtr = const_cast<float*>(in) + done;
        float* outPtr = out + done;
        float* inCh[1] = {inPtr};
        float* outCh[2] = {outPtr, outPtr};
        if (stereoOut) {  // an IR with two channels: keep the first, the second lands in a scratch block
            static thread_local std::vector<float> scratch;
            scratch.resize(m);
            outCh[1] = scratch.data();
        }
        d.process(inCh, outCh, int(m));
        done += m;
    }
}

void NamModel::reset() { impl_->dsp->Reset(impl_->fs, impl_->maxBlock); }

const std::string& NamModel::path() const { return impl_->path; }
const std::string& NamModel::name() const { return impl_->name; }
double NamModel::trainedRate() const {
    const double r = impl_->dsp->GetExpectedSampleRate();
    return r > 0.0 ? r : 0.0;
}
bool NamModel::rateMatches() const {
    const double r = trainedRate();
    return r <= 0.0 || impl_->dsp->SupportsArbitrarySampleRate() || std::fabs(r - impl_->fs) < 0.005 * r;
}
bool NamModel::hasLoudness() const { return impl_->dsp->HasLoudness(); }
double NamModel::loudnessDb() const { return impl_->dsp->HasLoudness() ? impl_->dsp->GetLoudness() : 0.0; }
float NamModel::normalizeGain() const {
    return impl_->dsp->HasLoudness() ? float(std::pow(10.0, (kTargetLoudnessDb - impl_->dsp->GetLoudness()) / 20.0)) : 1.0f;
}
double NamModel::inputLevelDbu() const {
    return impl_->dsp->HasInputLevel() ? impl_->dsp->GetInputLevel() : std::numeric_limits<double>::quiet_NaN();
}

}  // namespace sas
