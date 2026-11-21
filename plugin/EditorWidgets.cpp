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

// ------------------------------------------------------------------ look

juce::FontOptions AmpLookAndFeel::font(float size, bool bold, float tracking) {
    auto f = juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain);
    return tracking != 0.0f ? f.withKerningFactor(tracking) : f;
}

AmpLookAndFeel::AmpLookAndFeel() : theme_(EditorTheme::from(sas::ThemeDef{})) {
#if JUCE_WINDOWS
    setDefaultSansSerifTypefaceName("Segoe UI");
#elif JUCE_MAC
    setDefaultSansSerifTypefaceName("Helvetica Neue");
#endif
    setTheme(theme_);
}

void AmpLookAndFeel::setTheme(const EditorTheme& t) {
    theme_ = t;
    const auto body = t.panel.brighter(0.12f);
    setColour(juce::ResizableWindow::backgroundColourId, t.background);
    setColour(juce::Label::textColourId, t.text);
    setColour(juce::Slider::textBoxTextColourId, t.text.withAlpha(0.85f));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, t.accent.withAlpha(0.4f));
    setColour(juce::ComboBox::backgroundColourId, body);
    setColour(juce::ComboBox::outlineColourId, t.edge);
    setColour(juce::ComboBox::textColourId, t.text);
    setColour(juce::ComboBox::arrowColourId, t.accent);
    setColour(juce::PopupMenu::backgroundColourId, body);
    setColour(juce::PopupMenu::textColourId, t.text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, t.accent.withAlpha(0.35f));
    setColour(juce::PopupMenu::highlightedTextColourId, t.text);
    setColour(juce::TextButton::buttonColourId, body);
    setColour(juce::TextButton::buttonOnColourId, t.accent.withAlpha(0.25f));
    setColour(juce::TextButton::textColourOffId, t.text.withAlpha(0.75f));
    setColour(juce::TextButton::textColourOnId, t.accent);
    setColour(juce::ToggleButton::textColourId, t.text.withAlpha(0.85f));
    setColour(juce::ToggleButton::tickColourId, t.accent);
    setColour(juce::ListBox::backgroundColourId, t.panel);
    setColour(juce::ListBox::outlineColourId, t.edge);
    setColour(juce::Slider::thumbColourId, t.accent);
    setColour(juce::Slider::trackColourId, t.accent.withAlpha(0.45f));
    setColour(juce::Slider::backgroundColourId, t.edge);
    setColour(juce::TextEditor::backgroundColourId, body);
    setColour(juce::TextEditor::outlineColourId, t.edge);
    setColour(juce::TextEditor::focusedOutlineColourId, t.accent);
    setColour(juce::TextEditor::textColourId, t.text);
    setColour(juce::TextEditor::highlightColourId, t.accent.withAlpha(0.4f));
    setColour(juce::TextEditor::highlightedTextColourId, t.text);
    setColour(juce::CaretComponent::caretColourId, t.accent);
}

void AmpLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle,
                                      float endAngle, juce::Slider&) {
    const auto bounds = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(3.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float a = startAngle + (endAngle - startAngle) * pos;
    const auto& t = theme_;

    // Value ring on the outer edge, with a soft glow: a dark track and a bright arc up to the value (no tick marks).
    const float lineW = std::clamp(radius * 0.12f, 2.5f, 4.5f);
    const float arcR = radius - lineW * 0.5f - 1.0f;
    juce::Path track, value;
    track.addCentredArc(c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    value.addCentredArc(c.x, c.y, arcR, arcR, 0.0f, startAngle, std::max(a, startAngle + 0.001f), true);
    g.setColour(t.edge.withAlpha(0.75f));
    g.strokePath(track, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(t.accent.withAlpha(0.12f));
    g.strokePath(value, juce::PathStrokeType(lineW * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(t.accent.withAlpha(0.22f));
    g.strokePath(value, juce::PathStrokeType(lineW * 1.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(t.accent);
    g.strokePath(value, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Body: a shadow, a dark skirt and a cap.
    const float skirtR = arcR - lineW * 0.5f - 3.0f;
    const float capR = skirtR * 0.82f;
    juce::Path skirt;
    skirt.addEllipse(c.x - skirtR, c.y - skirtR, skirtR * 2, skirtR * 2);
    juce::DropShadow(juce::Colours::black.withAlpha(0.7f), 7, {0, 2}).drawForPath(g, skirt);
    g.setGradientFill(juce::ColourGradient(t.knobBottom.brighter(0.15f), c.x, c.y - skirtR, t.knobBottom.darker(0.6f), c.x, c.y + skirtR, false));
    g.fillPath(skirt);
    g.setColour(t.edge.withAlpha(0.9f));
    g.drawEllipse(c.x - skirtR, c.y - skirtR, skirtR * 2, skirtR * 2, 1.0f);

    g.setGradientFill(juce::ColourGradient(t.knobTop, c.x - capR * 0.5f, c.y - capR * 0.9f, t.knobBottom, c.x + capR * 0.4f, c.y + capR, false));
    g.fillEllipse(c.x - capR, c.y - capR, capR * 2, capR * 2);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.drawEllipse(c.x - capR, c.y - capR, capR * 2, capR * 2, 1.0f);
    // A faint highlight along the upper left rim of the cap.
    juce::Path hl;
    hl.addCentredArc(c.x, c.y, capR - 1.5f, capR - 1.5f, 0.0f, juce::degreesToRadians(-70.0f), juce::degreesToRadians(15.0f), true);
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.strokePath(hl, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Pointer.
    const juce::Point<float> d(std::sin(a), -std::cos(a));
    const juce::PathStrokeType pointerStroke(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path pointer;
    pointer.startNewSubPath(c + d * (capR * 0.35f));
    pointer.lineTo(c + d * (capR - 3.0f));
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.strokePath(pointer, pointerStroke, juce::AffineTransform::translation(0.0f, 1.0f));
    g.setColour(t.pointer);
    g.strokePath(pointer, pointerStroke);
}

void AmpLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) {
    const auto r = b.getLocalBounds().toFloat();
    const float h = std::min(14.0f, r.getHeight() - 2.0f), w = h * 1.9f;
    const auto track = juce::Rectangle<float>(r.getX() + 1.0f, r.getCentreY() - h * 0.5f, w, h);
    const bool on = b.getToggleState();
    const auto& t = theme_;
    g.setColour(on ? t.accent.withAlpha(0.30f) : juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(track, h * 0.5f);
    g.setColour(on ? t.accent : t.edge.brighter(highlighted ? 0.4f : 0.0f));
    g.drawRoundedRectangle(track.reduced(0.5f), h * 0.5f, 1.0f);
    const float kr = h * 0.5f - 2.0f;
    const float kx = on ? track.getRight() - h * 0.5f : track.getX() + h * 0.5f;
    if (on) {
        g.setColour(t.accent.withAlpha(0.35f));
        g.fillEllipse(kx - kr - 2.0f, track.getCentreY() - kr - 2.0f, (kr + 2.0f) * 2, (kr + 2.0f) * 2);
    }
    g.setColour(on ? t.accent.brighter(0.3f) : t.label);
    g.fillEllipse(kx - kr, track.getCentreY() - kr, kr * 2, kr * 2);
    g.setColour(b.findColour(juce::ToggleButton::textColourId));
    g.setFont(font(12.0f));
    g.drawText(b.getButtonText(), juce::Rectangle<float>(track.getRight() + 6.0f, r.getY(), r.getRight() - track.getRight() - 6.0f, r.getHeight()),
               juce::Justification::centredLeft);
}

// A recessed field, like the panels: a dark surface with a shadow along the top edge and a fine border.
static void paintInset(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour surface, juce::Colour edge, float corner) {
    g.setColour(surface);
    g.fillRoundedRectangle(b, corner);
    {
        juce::Graphics::ScopedSaveState state(g);
        juce::Path clip;
        clip.addRoundedRectangle(b, corner);
        g.reduceClipRegion(clip);
        g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.30f), 0.0f, b.getY(), juce::Colours::transparentBlack, 0.0f, b.getY() + 6.0f, false));
        g.fillRect(b.withHeight(6.0f));
    }
    g.setColour(edge);
    g.drawRoundedRectangle(b.reduced(0.5f), corner, 1.0f);
}

void AmpLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) {
    const auto b = juce::Rectangle<float>(0.0f, 0.0f, float(w), float(h));
    const auto& t = theme_;
    paintInset(g, b, t.background.darker(0.25f), box.hasKeyboardFocus(true) ? t.accent : t.edge, 5.0f);
    // A small chevron instead of the stock arrow box.
    const float cx = float(w) - 12.0f, cy = float(h) * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath(cx - 3.5f, cy - 1.5f);
    chevron.lineTo(cx, cy + 2.0f);
    chevron.lineTo(cx + 3.5f, cy - 1.5f);
    g.setColour(box.isEnabled() ? t.label : t.label.withAlpha(0.4f));
    g.strokePath(chevron, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void AmpLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down) {
    const auto b = button.getLocalBounds().toFloat();
    const auto& t = theme_;
    const bool on = button.getToggleState();
    auto surface = on ? t.accent.withAlpha(0.22f) : t.background.darker(0.15f).brighter(highlighted ? 0.08f : 0.0f);
    if (down) surface = surface.darker(0.25f);
    paintInset(g, b, surface, on ? t.accent : t.edge.brighter(highlighted ? 0.25f : 0.0f), 5.0f);
}

// A graphic EQ fader, like the slide controls of a Mark-series front panel: a narrow dark slot with a scale beside it (the middle mark is flat), the part of the slot
// between the middle and the cap lit, and a rectangular silver cap with a dark line across its middle.
void AmpLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle style,
                                      juce::Slider& s) {
    if (style != juce::Slider::LinearVertical) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }
    const auto& t = theme_;
    const float cx = float(x) + float(w) * 0.5f;
    const float capH = std::clamp(float(w) * 0.32f, 9.0f, 14.0f), capW = std::clamp(float(w) * 0.62f, 16.0f, 30.0f);
    const float top = float(y) + capH * 0.5f, bottom = float(y + h) - capH * 0.5f;
    // Scale: eleven ticks on both sides of the slot, the middle (flat) and the ends longer.
    for (int i = 0; i <= 10; ++i) {
        const float ty = top + (bottom - top) * float(i) / 10.0f;
        const bool major = i == 0 || i == 5 || i == 10;
        const float len = major ? capW * 0.5f : capW * 0.32f;
        g.setColour(t.label.withAlpha(major ? 0.75f : 0.35f));
        g.drawLine(cx - capW * 0.5f - 3.0f - len, ty, cx - capW * 0.5f - 3.0f, ty, major ? 1.2f : 1.0f);
        g.drawLine(cx + capW * 0.5f + 3.0f, ty, cx + capW * 0.5f + 3.0f + len, ty, major ? 1.2f : 1.0f);
    }
    // The slot: a recessed groove.
    const auto slot = juce::Rectangle<float>(cx - 2.5f, top - 2.0f, 5.0f, bottom - top + 4.0f);
    g.setColour(juce::Colours::black.withAlpha(0.75f));
    g.fillRoundedRectangle(slot, 2.5f);
    g.setColour(t.edge.withAlpha(0.9f));
    g.drawRoundedRectangle(slot.reduced(0.5f), 2.5f, 1.0f);
    // The lit part of the slot, from the middle (flat) to the cap.
    const float mid = (top + bottom) * 0.5f;
    const float py = std::clamp(pos, top, bottom);
    if (std::abs(py - mid) > 1.0f) {
        const auto lit = juce::Rectangle<float>(cx - 1.25f, std::min(py, mid), 2.5f, std::abs(py - mid));
        g.setColour(t.accent.withAlpha(0.25f));
        g.fillRoundedRectangle(lit.expanded(1.5f, 0.0f), 2.0f);
        g.setColour(t.accent);
        g.fillRoundedRectangle(lit, 1.25f);
    }
    // The cap: silver, with a shadow and a dark line across its middle.
    const auto cap = juce::Rectangle<float>(cx - capW * 0.5f, py - capH * 0.5f, capW, capH);
    juce::Path capPath;
    capPath.addRoundedRectangle(cap, 2.0f);
    juce::DropShadow(juce::Colours::black.withAlpha(0.7f), 5, {0, 2}).drawForPath(g, capPath);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe9ecef), cx, cap.getY(), juce::Colour(0xff8d939a), cx, cap.getBottom(), false));
    g.fillPath(capPath);
    g.setColour(juce::Colours::white.withAlpha(0.55f));
    g.drawHorizontalLine(int(cap.getY()) + 1, cap.getX() + 2.0f, cap.getRight() - 2.0f);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.drawRoundedRectangle(cap.reduced(0.5f), 2.0f, 1.0f);
    g.setColour(juce::Colour(0xff1b1d20));
    g.fillRect(cap.getX() + 2.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 4.0f, 2.0f);
}

// ------------------------------------------------------------------ switch

namespace {
float textWidth(const juce::FontOptions& f, const juce::String& s) {
    juce::GlyphArrangement ga;
    ga.addLineOfText(juce::Font(f), s, 0.0f, 0.0f);
    return ga.getBoundingBox(0, -1, true).getWidth();
}
}  // namespace

bool SwitchBox::segmented() const {
    const int n = getNumItems();
    if (n < 2 || n > 4) return false;
    const auto f = AmpLookAndFeel::font(11.0f, true);
    const float cell = float(getWidth()) / float(n);
    for (int i = 0; i < n; ++i)
        if (textWidth(f, getItemText(i)) + 14.0f > cell) return false;
    return true;
}

void SwitchBox::hideLabelIfSegmented() {
    const bool seg = dynamic_cast<const AmpLookAndFeel*>(&getLookAndFeel()) && segmented();
    for (auto* c : getChildren()) c->setVisible(!seg);
}

void SwitchBox::resized() {
    juce::ComboBox::resized();
    hideLabelIfSegmented();
}

void SwitchBox::paint(juce::Graphics& g) {
    const auto* look = dynamic_cast<const AmpLookAndFeel*>(&getLookAndFeel());
    const bool seg = look && segmented();
    hideLabelIfSegmented();
    if (!seg) {
        juce::ComboBox::paint(g);
        return;
    }
    const int n = getNumItems(), now = getSelectedItemIndex();
    const auto b = getLocalBounds().toFloat().reduced(0.5f);
    paintInset(g, b, look->background().darker(0.25f), look->panelEdge(), 5.0f);
    const float cell = b.getWidth() / float(n);
    for (int i = 0; i < n; ++i) {
        const auto r = juce::Rectangle<float>(b.getX() + cell * float(i), b.getY(), cell, b.getHeight());
        if (i == now) {
            g.setColour(look->accent().withAlpha(0.28f));
            g.fillRoundedRectangle(r.reduced(2.0f), 4.0f);
            g.setColour(look->accent());
            g.drawRoundedRectangle(r.reduced(2.0f), 4.0f, 1.2f);
        }
        g.setColour(i == now ? look->text() : look->label());
        g.setFont(AmpLookAndFeel::font(11.0f, true));
        g.drawText(getItemText(i), r.toNearestInt(), juce::Justification::centred);
    }
}

void SwitchBox::mouseDown(const juce::MouseEvent& e) {
    if (!segmented()) {
        juce::ComboBox::mouseDown(e);
        return;
    }
    if (!isEnabled()) return;
    const int i = juce::jlimit(0, getNumItems() - 1, int(float(e.x) / float(getWidth()) * float(getNumItems())));
    if (i != getSelectedItemIndex()) setSelectedItemIndex(i, juce::sendNotificationSync);
    repaint();
}

void SwitchBox::mouseUp(const juce::MouseEvent& e) {
    if (!segmented()) juce::ComboBox::mouseUp(e);
}

// ------------------------------------------------------------------ knob

KnobWithLabel::KnobWithLabel(juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& caption, bool fader) {
    if (dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(paramId)) != nullptr) {
        // An on / off parameter is a switch under its caption.
        buttonAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, paramId, toggle_);
        toggle_.setButtonText(tr(caption));
        addAndMakeVisible(toggle_);
        return;
    }
    if (fader) slider_.setSliderStyle(juce::Slider::LinearVertical);
    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, paramId, slider_);
    slider_.setRotaryParameters(juce::degreesToRadians(225.0f), juce::degreesToRadians(495.0f), true);
    slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 14);
    if (fader) {  // all the height for the travel: the value shows in a bubble while it moves
        slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider_.setPopupDisplayEnabled(true, true, nullptr, 900);
    }
    slider_.setDoubleClickReturnValue(true, apvts.getParameter(paramId)->convertFrom0to1(apvts.getParameter(paramId)->getDefaultValue()));
    slider_.setMouseDragSensitivity(160);
    slider_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(slider_);
    caption_.setText(tr(caption), juce::dontSendNotification);
    caption_.setJustificationType(juce::Justification::centred);
    caption_.setFont(AmpLookAndFeel::font(10.5f, true, 0.1f));
    addAndMakeVisible(caption_);
}

void KnobWithLabel::lookAndFeelChanged() {
    caption_.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId));
}

void KnobWithLabel::resized() {
    auto r = getLocalBounds();
    if (buttonAttachment_) {
        toggle_.setBounds(r.withSizeKeepingCentre(std::min(r.getWidth(), 90), 24));
        return;
    }
    caption_.setBounds(r.removeFromTop(16));
    slider_.setBounds(r);
}

// ------------------------------------------------------------------ level bar

void LevelBar::setLevel(float peak) {
    const float now = 20.0f * std::log10(std::max(peak, 1e-5f));
    db_ = now > db_ ? now : std::max(now, db_ - 1.6f);
    if (peak >= 0.999f) clip_ = 40;
    else if (clip_ > 0) --clip_;
    repaint();
}

void LevelBar::paint(juce::Graphics& g) {
    const auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colour::fromRGB(14, 12, 10));
    g.fillRoundedRectangle(b, 2.0f);
    const float f = juce::jlimit(0.0f, 1.0f, (db_ + 60.0f) / 60.0f);
    if (getWidth() > getHeight()) {  // horizontal: -60 dBFS on the left, 0 on the right
        const float x0 = b.getX() + 1.0f, w = b.getWidth() - 2.0f, fx = x0 + w * f;
        auto xAt = [&](float db) { return x0 + w * (db + 60.0f) / 60.0f; };
        auto band = [&](float xl, float xr, juce::Colour col) {
            const float r = std::min(xr, fx);
            if (r > xl) { g.setColour(col); g.fillRect(xl, b.getY() + 1.0f, r - xl, b.getHeight() - 2.0f); }
        };
        band(x0, xAt(-18.0f), juce::Colour::fromRGB(80, 200, 100));
        band(xAt(-18.0f), xAt(-6.0f), juce::Colour::fromRGB(240, 160, 40));
        band(xAt(-6.0f), x0 + w, juce::Colour::fromRGB(230, 70, 50));
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawVerticalLine(int(xAt(-18.0f)), b.getY(), b.getBottom());
        g.drawVerticalLine(int(xAt(-6.0f)), b.getY(), b.getBottom());
        if (clip_ > 0) {
            g.setColour(juce::Colour::fromRGB(230, 70, 50));
            g.fillRect(b.getRight() - 4.0f, b.getY() + 1.0f, 3.0f, b.getHeight() - 2.0f);
        }
        return;
    }
    const float h = (b.getHeight() - 2.0f) * f;
    const float top = b.getBottom() - 1.0f - h;
    auto colourAt = [](float db) {
        return db > -6.0f ? juce::Colour::fromRGB(230, 70, 50) : db > -18.0f ? juce::Colour::fromRGB(240, 160, 40) : juce::Colour::fromRGB(80, 200, 100);
    };
    // Drawn in three bands so the colour follows the level.
    const float y18 = b.getBottom() - 1.0f - (b.getHeight() - 2.0f) * (42.0f / 60.0f);
    const float y6 = b.getBottom() - 1.0f - (b.getHeight() - 2.0f) * (54.0f / 60.0f);
    auto band = [&](float yTop, float yBot, juce::Colour col) {
        const float t = std::max(yTop, top);
        if (t < yBot) { g.setColour(col); g.fillRect(b.getX() + 1.0f, t, b.getWidth() - 2.0f, yBot - t); }
    };
    band(y18, b.getBottom() - 1.0f, colourAt(-30.0f));
    band(y6, y18, colourAt(-12.0f));
    band(b.getY() + 1.0f, y6, colourAt(-3.0f));
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawHorizontalLine(int(y18), b.getX(), b.getRight());
    g.drawHorizontalLine(int(y6), b.getX(), b.getRight());
    if (clip_ > 0) {
        g.setColour(juce::Colour::fromRGB(230, 70, 50));
        g.fillRect(b.getX() + 1.0f, b.getY() + 1.0f, b.getWidth() - 2.0f, 3.0f);
    }
}

// ------------------------------------------------------------------ IR curve

void IrCurve::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    auto* look = dynamic_cast<AmpLookAndFeel*>(&getLookAndFeel());
    const auto accent = look ? look->accent() : juce::Colours::orange;
    const auto label = look ? look->label() : juce::Colours::grey;
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(b, 4.0f);
    const auto plot = b.reduced(6.0f, 6.0f).withTrimmedBottom(10.0f);
    // Grid: 100 Hz, 1 kHz, 10 kHz and 0 dB, +-10 dB.
    g.setFont(AmpLookAndFeel::font(9.0f));
    for (double f : {100.0, 1000.0, 10000.0}) {
        const float x = plot.getX() + plot.getWidth() * float(std::log(f / 50.0) / std::log(200.0));
        g.setColour(label.withAlpha(0.25f));
        g.drawVerticalLine(int(x), plot.getY(), plot.getBottom());
        g.setColour(label.withAlpha(0.8f));
        g.drawText(f >= 1000.0 ? juce::String(int(f / 1000.0)) + "k" : juce::String(int(f)), int(x) - 14, int(plot.getBottom()) + 1, 28, 10, juce::Justification::centred);
    }
    for (float db : {-10.0f, 0.0f, 10.0f}) {
        const float y = plot.getCentreY() - db / 15.0f * plot.getHeight() * 0.5f;
        g.setColour(label.withAlpha(db == 0.0f ? 0.35f : 0.18f));
        g.drawHorizontalLine(int(y), plot.getX(), plot.getRight());
    }
    if (curve_.size() < 2) {
        g.setColour(label);
        g.setFont(AmpLookAndFeel::font(11.0f));
        g.drawText(tr("no impulse loaded"), b, juce::Justification::centred);
        return;
    }
    juce::Path path;
    for (std::size_t i = 0; i < curve_.size(); ++i) {
        const float x = plot.getX() + plot.getWidth() * float(i) / float(curve_.size() - 1);
        const float y = plot.getCentreY() - juce::jlimit(-15.0f, 15.0f, curve_[i]) / 15.0f * plot.getHeight() * 0.5f;
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.setColour(accent.withAlpha(0.25f));
    g.strokePath(path, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(accent);
    g.strokePath(path, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ------------------------------------------------------------------ tuner display

void TunerDisplay::setReading(bool valid, bool active, double hz, double referenceA) {
    valid_ = valid;
    active_ = active;
    hz_ = hz;
    if (valid) {
        note_ = sas::noteFor(hz, referenceA);
        // The needle glides, so a trembling string does not make it jitter.
        needle_ += 0.45f * (float(note_.cents) - needle_);
    }
    repaint();
}

void TunerDisplay::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    auto* look = dynamic_cast<AmpLookAndFeel*>(&getLookAndFeel());
    const auto accent = look ? look->accent() : juce::Colours::orange;
    const auto label = look ? look->label() : juce::Colours::grey;
    const auto text = look ? look->text() : juce::Colours::white;
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(b, 6.0f);

    if (!active_ || !valid_) {
        g.setColour(label);
        g.setFont(AmpLookAndFeel::font(15.0f));
        g.drawText(active_ ? tr("play a note") : tr("press  Tuner on  to start"), b.withTrimmedBottom(40.0f), juce::Justification::centred);
        if (active_) {
            g.setFont(AmpLookAndFeel::font(80.0f, true));
            g.setColour(label.withAlpha(0.25f));
            g.drawText("-", b.withTrimmedBottom(40.0f).translated(0.0f, -20.0f), juce::Justification::centred);
        }
    }

    const bool inTune = valid_ && std::abs(needle_) < 3.0f;
    const auto lit = inTune ? juce::Colour::fromRGB(90, 210, 110) : accent;

    // The note, big, with its octave.
    if (active_ && valid_) {
        auto top = b.withTrimmedBottom(b.getHeight() * 0.42f);
        g.setColour(lit);
        g.setFont(AmpLookAndFeel::font(std::min(110.0f, top.getHeight() * 0.95f), true));
        const juce::String name(note_.name);
        g.drawText(name, top.translated(-18.0f, 0.0f), juce::Justification::centred);
        g.setColour(text.withAlpha(0.7f));
        g.setFont(AmpLookAndFeel::font(24.0f, true));
        g.drawText(juce::String(note_.octave), top.translated(top.getWidth() * 0.18f + (name.length() > 1 ? 24.0f : 4.0f), top.getHeight() * 0.22f), juce::Justification::centred);
    }

    // The needle meter: +-50 cents, ticks every 10, the centre mark taller.
    auto meter = b.withTrimmedTop(b.getHeight() * 0.60f).reduced(24.0f, 0.0f).withHeight(34.0f).withY(b.getY() + b.getHeight() * 0.60f);
    const float cx = meter.getCentreX(), half = meter.getWidth() * 0.5f;
    for (int c = -50; c <= 50; c += 10) {
        const float x = cx + half * float(c) / 50.0f;
        const bool centre = c == 0;
        g.setColour((centre ? lit : label).withAlpha(centre ? 0.95f : 0.6f));
        g.drawLine(x, meter.getY() + (centre ? 0.0f : 8.0f), x, meter.getBottom(), centre ? 2.0f : 1.2f);
    }
    g.setColour(label.withAlpha(0.5f));
    g.drawLine(meter.getX(), meter.getBottom(), meter.getRight(), meter.getBottom(), 1.0f);
    g.setFont(AmpLookAndFeel::font(11.0f, true, 0.1f));
    g.setColour(label);
    g.drawText(tr("FLAT"), juce::Rectangle<float>(meter.getX(), meter.getBottom() + 4.0f, 60.0f, 14.0f), juce::Justification::centredLeft);
    g.drawText(tr("SHARP"), juce::Rectangle<float>(meter.getRight() - 60.0f, meter.getBottom() + 4.0f, 60.0f, 14.0f), juce::Justification::centredRight);
    if (active_ && valid_) {
        const float x = cx + half * juce::jlimit(-50.0f, 50.0f, needle_) / 50.0f;
        juce::Path needle;
        needle.addTriangle(x - 8.0f, meter.getY() - 10.0f, x + 8.0f, meter.getY() - 10.0f, x, meter.getY() + 8.0f);
        g.setColour(lit.withAlpha(0.3f));
        g.fillEllipse(x - 14.0f, meter.getY() - 12.0f, 28.0f, 28.0f);
        g.setColour(lit);
        g.fillPath(needle);
        g.setFont(AmpLookAndFeel::font(15.0f, true));
        g.setColour(text);
        const int cents = int(std::lround(note_.cents));
        g.drawText(juce::String(cents > 0 ? "+" : "") + juce::String(cents) + " cents   -   " + juce::String(hz_, 1) + " Hz",
                   juce::Rectangle<float>(b.getX(), meter.getBottom() + 22.0f, b.getWidth(), 20.0f), juce::Justification::centred);
    }
}

// ------------------------------------------------------------------ tube glow

void TubeGlow::setTubes(int count, const juce::String& name) {
    if (count == count_ && name == name_ && !transistors_) return;
    count_ = std::max(1, count);
    name_ = name;
    transistors_ = false;
    repaint();
}

void TubeGlow::setTransistors(int count, const juce::String& name) {
    if (count == count_ && name == name_ && transistors_) return;
    count_ = std::max(1, count);
    name_ = name;
    transistors_ = true;
    mA_ = 0.0f;
    repaint();
}

void TubeGlow::setGlow(float level, float mA) {
    level_ = level;
    mA_ = mA;
    shown_ += 0.30f * (level_ - shown_);  // the glow follows the current with a little inertia, like a hot plate
    base_ += 0.012f * (level_ - base_);   // and what it has idled at lately: a change from that is what the picture exaggerates
    repaint();
}

// The tubes are four pictures of the same size (plugin/tubes/tube.vectorcraft), drawn one over the other: the glass, the plates, the base and the pins;
// the heater, always lit; the plate, whose glow follows the current; and the reflections of the glass on top.
void TubeGlow::paint(juce::Graphics& g) {
    static const juce::Image back = juce::ImageCache::getFromMemory(TubeData::tube_back_png, TubeData::tube_back_pngSize);
    static const juce::Image heater = juce::ImageCache::getFromMemory(TubeData::tube_heater_png, TubeData::tube_heater_pngSize);
    static const juce::Image plate = juce::ImageCache::getFromMemory(TubeData::tube_plate_png, TubeData::tube_plate_pngSize);
    static const juce::Image front = juce::ImageCache::getFromMemory(TubeData::tube_front_png, TubeData::tube_front_pngSize);
    static const juce::Image transistor = juce::ImageCache::getFromMemory(TubeData::transistor_png, TubeData::transistor_pngSize);
    constexpr float kAspect = 160.0f / 480.0f;  // the pictures' width over their height

    auto r = getLocalBounds().toFloat();
    auto text = r.removeFromBottom(16.0f);
    const float cell = r.getWidth() / float(count_);
    float h = std::max(10.0f, r.getHeight() - 2.0f), w = h * kAspect;
    if (w > cell - 4.0f) w = std::max(8.0f, cell - 4.0f), h = w / kAspect;
    if (transistors_) {
        // Power transistors (plugin/tubes/transistor.vectorcraft): the same picture for each, side by side where the tubes would stand.
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.setOpacity(1.0f);
        for (int i = 0; i < count_; ++i) {
            const float cx = r.getX() + cell * (float(i) + 0.5f);
            g.drawImage(transistor, juce::Rectangle<float>(cx - w * 0.5f, r.getBottom() - h, w, h));
        }
        g.setColour(getLookAndFeel().findColour(juce::Label::textColourId).withAlpha(0.75f));
        g.setFont(AmpLookAndFeel::font(11.0f));
        g.drawText(name_, text, juce::Justification::centred);
        return;
    }
    // The glow is the current, and a change of the current is shown three times as strong: when the amp is pushed (the plates draw more) the tubes
    // flare up well past where they idle, and they settle back as it eases. It goes a little above 1 for the flare.
    const float glow = juce::jlimit(0.0f, 1.4f, shown_ * 1.1f + 3.0f * (shown_ - base_));
    const float plateAlpha = std::pow(juce::jlimit(0.0f, 1.0f, glow), 1.6f);  // a plate that carries a typical idle current is only warm; it is orange when pushed
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    for (int i = 0; i < count_; ++i) {
        const float cx = r.getX() + cell * (float(i) + 0.5f);
        const auto dest = juce::Rectangle<float>(cx - w * 0.5f, r.getBottom() - h, w, h);
        const juce::Point<float> centre(cx, dest.getY() + h * 0.40f);
        {
            // The halo: warm light spilling out of the glass over the panel behind it. It grows and brightens with the glow, taller than wide.
            const float reach = w * (0.7f + 0.85f * glow);
            const auto warm = juce::Colour::fromRGB(255, 104, 28);
            juce::ColourGradient halo(warm.withAlpha(std::min(0.7f, 0.14f + 0.48f * glow)), centre, warm.withAlpha(0.0f), centre.translated(reach, 0.0f), true);
            halo.addColour(0.45, warm.withAlpha(std::min(0.4f, 0.06f + 0.25f * glow)));
            juce::FillType fill(halo);
            fill.transform = juce::AffineTransform::scale(1.0f, 1.9f, centre.x, centre.y);
            g.setFillType(fill);
            g.fillRect(juce::Rectangle<float>(centre.x - reach, centre.y - reach * 1.9f, reach * 2.0f, reach * 3.8f));
        }
        g.setOpacity(1.0f);
        g.drawImage(back, dest);
        g.setOpacity(0.9f);
        g.drawImage(heater, dest);
        g.setOpacity(plateAlpha);
        g.drawImage(plate, dest);
        g.setOpacity(1.0f);
        g.drawImage(front, dest);
        if (glow > 0.75f) {
            // Overexposed: the plate burns white-hot in the middle when the tube flares.
            const float bloom = std::min(0.7f, (glow - 0.75f) * 1.4f);
            const auto hot = juce::Colour::fromRGB(255, 226, 170);
            const juce::Point<float> plateCentre(cx, dest.getY() + h * 0.43f);
            juce::ColourGradient flare(hot.withAlpha(bloom), plateCentre, hot.withAlpha(0.0f), plateCentre.translated(w * 0.42f, 0.0f), true);
            juce::FillType fill(flare);
            fill.transform = juce::AffineTransform::scale(1.0f, 2.6f, plateCentre.x, plateCentre.y);
            g.setFillType(fill);
            g.fillRect(juce::Rectangle<float>(plateCentre.x - w * 0.42f, plateCentre.y - w * 1.1f, w * 0.84f, w * 2.2f));
        }
    }
    g.setColour(getLookAndFeel().findColour(juce::Label::textColourId).withAlpha(0.75f));
    g.setFont(AmpLookAndFeel::font(11.0f));
    g.drawText(name_ + (mA_ > 0.5f ? juce::String(" - ") + juce::String(juce::roundToInt(mA_)) + " mA" : juce::String("")), text, juce::Justification::centred);
}

// ------------------------------------------------------------------ panels

juce::String irAlignText(const SasProcessor::IrAlignInfo& info) {
    if (!info.valid) return {};
    juce::StringArray parts;
    if (info.invertB) parts.add(tr("B flipped"));
    const double ms = std::max(info.delayAms, info.delayBms);
    if (ms >= 0.005) parts.add(juce::String(info.delayAms > 0.0 ? "A" : "B") + " +" + juce::String(ms, 2) + " ms");
    return parts.isEmpty() ? juce::String(tr("already in phase")) : parts.joinIntoString(", ");
}

void paintPanelBoxes(juce::Graphics& g, const AmpLookAndFeel& look, const std::vector<PanelBox>& panels) {
    for (const auto& p : panels) {
        const auto b = p.bounds.toFloat();
        constexpr float corner = 5.0f;
        // A recessed surface: nearly flat, a soft shadow falling from the top edge, a faint light line along the bottom edge.
        g.setGradientFill(juce::ColourGradient(look.panel().darker(0.04f), 0.0f, b.getY(), look.panel().darker(0.10f), 0.0f, b.getBottom(), false));
        g.fillRoundedRectangle(b, corner);
        {
            juce::Graphics::ScopedSaveState state(g);
            juce::Path clip;
            clip.addRoundedRectangle(b, corner);
            g.reduceClipRegion(clip);
            g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.32f), 0.0f, b.getY(), juce::Colours::transparentBlack, 0.0f, b.getY() + 9.0f, false));
            g.fillRect(b.withHeight(9.0f));
            g.setColour(juce::Colours::white.withAlpha(0.045f));
            g.fillRect(b.getX() + 2.0f, b.getBottom() - 1.0f, b.getWidth() - 4.0f, 1.0f);
        }
        g.setColour(look.panelEdge().withAlpha(0.7f));
        g.drawRoundedRectangle(b.reduced(0.5f), corner, 1.0f);
        if (p.caption.isNotEmpty()) {
            g.setColour(look.label().withAlpha(0.85f));
            g.setFont(AmpLookAndFeel::font(10.0f, true, 0.12f));
            g.drawText(tr(p.caption), p.bounds.getX() + 12, p.bounds.getY() + 4, p.bounds.getWidth() - 22, 18, juce::Justification::centredLeft);
        }
    }
}

