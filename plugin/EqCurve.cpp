#include "I18n.h"
#include "PluginEditor.h"
#include "IconData.h"
#include "TubeData.h"
#include "ParallelPanels.h"
#include "RackPage.h"
#include "RackState.h"
#include "EqPresets.h"
#include "FxPresets.h"

#include <cmath>

#include "sas/version.h"

// ------------------------------------------------------------------ equaliser curve

EqCurve::EqCurve(juce::AudioProcessorValueTreeState& apvts, int instance) : apvts_(apvts), instance_(instance) { setRepaintsOnMouseActivity(true); }

juce::String EqCurve::pid(const char* baseId) const { return RackState::paramId(baseId, instance_); }

sas::EqParams EqCurve::params() const {
    sas::EqParams p;
    p.on = apvts_.getRawParameterValue(pid(ids::eqOn))->load() > 0.5f;
    p.hpfHz = double(apvts_.getRawParameterValue(pid(ids::eqHpf))->load());
    p.lpfHz = double(apvts_.getRawParameterValue(pid(ids::eqLpf))->load());
    for (int b = 0; b < sas::EqParams::kBands; ++b) {
        p.band[std::size_t(b)].freq = double(apvts_.getRawParameterValue(pid(ids::eqFreq[b]))->load());
        p.band[std::size_t(b)].gainDb = double(apvts_.getRawParameterValue(pid(ids::eqGain[b]))->load());
        p.band[std::size_t(b)].q = double(apvts_.getRawParameterValue(pid(ids::eqQ[b]))->load());
    }
    return p;
}

constexpr double kEqRangeDb = 18.0;
float EqCurve::xOf(double hz) const { return float(std::log10(hz / 20.0) / 3.0 * double(getWidth() - 1)); }
float EqCurve::yOf(double db) const { return float((1.0 - (db + kEqRangeDb) / (2.0 * kEqRangeDb)) * double(getHeight() - 1)); }
double EqCurve::hzAt(float x) const { return 20.0 * std::pow(10.0, 3.0 * double(x) / double(std::max(1, getWidth() - 1))); }
double EqCurve::dbAt(float y) const { return (1.0 - double(y) / double(std::max(1, getHeight() - 1))) * 2.0 * kEqRangeDb - kEqRangeDb; }

void EqCurve::select(int band) {
    selected_ = juce::jlimit(0, 4, band);
    repaint();
}

int EqCurve::nodeAt(juce::Point<float> p) const {
    const auto prm = params();
    int best = -1;
    float bestD = 16.0f;
    for (int b = 0; b < sas::EqParams::kBands; ++b) {
        const float d = p.getDistanceFrom({xOf(prm.band[std::size_t(b)].freq), yOf(prm.band[std::size_t(b)].gainDb)});
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

void EqCurve::setPlain(const juce::String& id, double value, bool gesture) {
    if (auto* q = apvts_.getParameter(id)) {
        if (gesture) q->beginChangeGesture();
        q->setValueNotifyingHost(q->convertTo0to1(float(value)));
        if (gesture) q->endChangeGesture();
    }
}

void EqCurve::mouseDown(const juce::MouseEvent& e) {
    const int hit = nodeAt(e.position);
    if (hit >= 0) {
        dragging_ = hit;
        selected_ = hit;
        if (onSelect) onSelect(hit);
        for (const auto& id : {pid(ids::eqFreq[hit]), pid(ids::eqGain[hit])})
            if (auto* q = apvts_.getParameter(id)) q->beginChangeGesture();
    }
}

void EqCurve::mouseDrag(const juce::MouseEvent& e) {
    if (dragging_ < 0) return;
    const auto fr = apvts_.getParameterRange(pid(ids::eqFreq[dragging_])), gr = apvts_.getParameterRange(pid(ids::eqGain[dragging_]));
    setPlain(pid(ids::eqFreq[dragging_]), juce::jlimit(double(fr.start), double(fr.end), hzAt(e.position.x)), false);
    setPlain(pid(ids::eqGain[dragging_]), juce::jlimit(double(gr.start), double(gr.end), dbAt(e.position.y)), false);
}

void EqCurve::mouseUp(const juce::MouseEvent&) {
    if (dragging_ < 0) return;
    for (const auto& id : {pid(ids::eqFreq[dragging_]), pid(ids::eqGain[dragging_])})
        if (auto* q = apvts_.getParameter(id)) q->endChangeGesture();
    dragging_ = -1;
}

void EqCurve::mouseDoubleClick(const juce::MouseEvent& e) {
    const int hit = nodeAt(e.position);
    if (hit >= 0) setPlain(pid(ids::eqGain[hit]), 0.0, true);
}

void EqCurve::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    int band = nodeAt(e.position);
    if (band < 0) band = selected_;
    const auto r = apvts_.getParameterRange(pid(ids::eqQ[band]));
    const double q = double(apvts_.getRawParameterValue(pid(ids::eqQ[band]))->load()) * std::pow(1.25, double(w.deltaY) * 4.0);
    setPlain(pid(ids::eqQ[band]), juce::jlimit(double(r.start), double(r.end), q), true);
}

void EqCurve::paint(juce::Graphics& g) {
    const auto* look = dynamic_cast<AmpLookAndFeel*>(&getLookAndFeel());
    const auto accent = look ? look->accent() : juce::Colours::orange, text = look ? look->text() : juce::Colours::white, edge = look ? look->panelEdge() : juce::Colours::grey;
    const auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.30f));
    g.fillRoundedRectangle(b, 5.0f);
    // What is sounding, behind everything else: a silhouette of the output spectrum (the level axis is its own: -96 to -12 dB).
    if (spectrum_ != nullptr) {
        constexpr float kLow = -96.0f, kHigh = -12.0f;
        juce::Path shape;
        const float bottom = b.getBottom();
        shape.startNewSubPath(xOf(sas::SpectrumAnalyzer::freqOf(0)), bottom);
        for (int i = 0; i < sas::SpectrumAnalyzer::kPoints; ++i) {
            const float level = juce::jlimit(0.0f, 1.0f, ((*spectrum_)[std::size_t(i)] - kLow) / (kHigh - kLow));
            shape.lineTo(xOf(sas::SpectrumAnalyzer::freqOf(i)), bottom - level * (b.getHeight() - 2.0f));
        }
        shape.lineTo(xOf(sas::SpectrumAnalyzer::freqOf(sas::SpectrumAnalyzer::kPoints - 1)), bottom);
        shape.closeSubPath();
        g.setGradientFill(juce::ColourGradient(text.withAlpha(0.34f), 0.0f, b.getY(), text.withAlpha(0.06f), 0.0f, bottom, false));
        g.fillPath(shape);
        g.setColour(text.withAlpha(0.45f));
        g.strokePath(shape, juce::PathStrokeType(1.0f));
    }
    // Grid: the usual guitar frequencies, 6 dB steps.
    g.setFont(AmpLookAndFeel::font(10.0f, false, 0.02f));
    for (double hz : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0}) {
        const float x = xOf(hz);
        g.setColour(edge.withAlpha(hz == 100.0 || hz == 1000.0 || hz == 10000.0 ? 0.55f : 0.25f));
        g.drawVerticalLine(int(x), 0.0f, b.getHeight());
        g.setColour(text.withAlpha(0.45f));
        g.drawText(hz >= 1000.0 ? juce::String(int(hz / 1000.0)) + "k" : juce::String(int(hz)), int(x) + 3, int(b.getBottom()) - 14, 34, 12, juce::Justification::left);
    }
    for (double db = -12.0; db <= 12.0; db += 6.0) {
        g.setColour(edge.withAlpha(db == 0.0 ? 0.7f : 0.25f));
        g.drawHorizontalLine(int(yOf(db)), 0.0f, b.getWidth());
        g.setColour(text.withAlpha(0.45f));
        g.drawText(juce::String(int(db)), 3, int(yOf(db)) - 12, 24, 12, juce::Justification::left);
    }
    // The curve, filled to the 0 dB line.
    sas::ParametricEq eq;
    eq.prepare(48000.0);
    const auto prm = params();
    eq.setParams(prm);
    juce::Path curve;
    for (int x = 0; x < getWidth(); ++x) {
        const double db = juce::jlimit(-kEqRangeDb, kEqRangeDb, prm.on ? eq.responseDb(hzAt(float(x))) : 0.0);
        if (x == 0) curve.startNewSubPath(0.0f, yOf(db)); else curve.lineTo(float(x), yOf(db));
    }
    juce::Path fill(curve);
    fill.lineTo(b.getRight(), yOf(0.0));
    fill.lineTo(0.0f, yOf(0.0));
    fill.closeSubPath();
    g.setColour(accent.withAlpha(prm.on ? 0.16f : 0.05f));
    g.fillPath(fill);
    g.setColour(accent.withAlpha(prm.on ? 1.0f : 0.4f));
    g.strokePath(curve, juce::PathStrokeType(2.0f));
    // The nodes.
    for (int i = 0; i < sas::EqParams::kBands; ++i) {
        const juce::Point<float> p(xOf(prm.band[std::size_t(i)].freq), yOf(prm.band[std::size_t(i)].gainDb));
        const bool sel = i == selected_;
        const float r = sel ? 9.0f : 7.0f;
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillEllipse(p.x - r, p.y - r, 2.0f * r, 2.0f * r);
        g.setColour(sel ? accent : accent.withAlpha(0.75f));
        g.drawEllipse(p.x - r, p.y - r, 2.0f * r, 2.0f * r, sel ? 2.5f : 1.5f);
        g.setFont(AmpLookAndFeel::font(10.0f, true));
        g.setColour(text);
        g.drawText(juce::String(i + 1), int(p.x) - 8, int(p.y) - 6, 16, 12, juce::Justification::centred);
    }
}

