#include "AmpFace.h"

#include "PluginProcessor.h"

#include <algorithm>

#ifdef SAS_HAS_FACES
#include "BinaryData.h"
#endif

namespace {
juce::Image loadFace(const std::string& ampId, const char* name) {
#ifdef SAS_HAS_FACES
    int size = 0;
    const std::string resource = ampId + "_" + name + "_png";
    if (const char* data = BinaryData::getNamedResource(resource.c_str(), size)) return juce::ImageCache::getFromMemory(data, size);
#else
    juce::ignoreUnused(ampId, name);
#endif
    return {};
}
}  // namespace

// The knob: the artwork's knob image (pointing up), turned to the value.
class AmpFace::KnobLook final : public juce::LookAndFeel_V4 {
public:
    explicit KnobLook(juce::Image knob) : knob_(std::move(knob)) {}
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override {
        const auto b = juce::Rectangle<float>(float(x), float(y), float(w), float(h));
        const float a = startAngle + (endAngle - startAngle) * pos;
        const float k = std::min(b.getWidth() / float(knob_.getWidth()), b.getHeight() / float(knob_.getHeight()));
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImageTransformed(knob_, juce::AffineTransform::scale(k).rotated(a, b.getCentreX() - float(knob_.getWidth()) * k * 0.0f, b.getCentreY())
                                          .translated(b.getCentreX() - float(knob_.getWidth()) * k * 0.5f, b.getCentreY() - float(knob_.getHeight()) * k * 0.5f));
    }

private:
    juce::Image knob_;
};

bool AmpFace::available(const sas::AmpDef& def) {
    return def.faceplate.style == "image" && loadFace(def.id, "background").isValid() && loadFace(def.id, "knob").isValid();
}

AmpFace::AmpFace(juce::AudioProcessorValueTreeState& apvts, const sas::AmpDef& def)
    : apvts_(apvts), plate_(def.faceplate), background_(loadFace(def.id, "background")), knob_(loadFace(def.id, "knob")), look_(std::make_unique<KnobLook>(knob_)) {
    for (const auto& u : def.ui) {
        auto k = std::make_unique<Knob>();
        k->slider.setLookAndFeel(look_.get());
        k->slider.setRotaryParameters(juce::degreesToRadians(225.0f), juce::degreesToRadians(495.0f), true);
        k->slider.setPopupDisplayEnabled(true, true, nullptr);
        k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, u.param, k->slider);
        addAndMakeVisible(k->slider);
        knobs_.push_back(std::move(k));
    }
}

AmpFace::~AmpFace() {
    for (auto& k : knobs_) k->slider.setLookAndFeel(nullptr);
}

juce::Rectangle<float> AmpFace::artBounds() const {
    const auto b = getLocalBounds().toFloat();
    const float s = std::min(b.getWidth() / float(plate_.artW), b.getHeight() / float(plate_.artH));
    return juce::Rectangle<float>(float(plate_.artW) * s, float(plate_.artH) * s).withCentre(b.getCentre());
}

juce::Point<float> AmpFace::toScreen(std::pair<double, double> p) const {
    const auto a = artBounds();
    const float s = a.getWidth() / float(plate_.artW);
    return {a.getX() + float(p.first) * s, a.getY() + float(p.second) * s};
}

void AmpFace::resized() {
    const float s = artBounds().getWidth() / float(plate_.artW);
    const float side = float(plate_.knobBox) * s;
    for (std::size_t i = 0; i < knobs_.size() && i < plate_.knobs.size(); ++i)
        knobs_[i]->slider.setBounds(juce::Rectangle<float>(side, side).withCentre(toScreen(plate_.knobs[i])).toNearestInt());
}

void AmpFace::paint(juce::Graphics& g) {
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(background_, artBounds());
    // The jack in use is ringed in orange.
    const int active = int(apvts_.getRawParameterValue(ids::jack)->load() + 0.5f);
    const float r = float(plate_.jackRadius) * artBounds().getWidth() / float(plate_.artW);
    for (std::size_t i = 0; i < plate_.jacks.size(); ++i) {
        const auto& values = plate_.jackValues[i];
        if (std::find(values.begin(), values.end(), active) == values.end()) continue;
        const auto c = toScreen(plate_.jacks[i]);
        g.setColour(juce::Colour(0xffff5a2a));
        g.drawEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f, 2.2f);
    }
}

int AmpFace::jackAt(juce::Point<int> pt) const {
    const float r = float(plate_.jackRadius) * artBounds().getWidth() / float(plate_.artW);
    for (std::size_t i = 0; i < plate_.jacks.size(); ++i)
        if (pt.toFloat().getDistanceFrom(toScreen(plate_.jacks[i])) < r * 1.2f) return plate_.jackValues[i].front();
    return -1;
}

void AmpFace::mouseDown(const juce::MouseEvent& e) {
    const int jack = jackAt(e.getPosition());
    if (jack < 0) return;
    if (auto* p = apvts_.getParameter(ids::jack)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(float(jack)));
        p->endChangeGesture();
    }
    repaint();
}
