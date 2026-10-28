#include "PluginEditor.h"

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

    // Tick marks 0..10 around the knob (the ends and the middle longer).
    constexpr int ticks = 11;
    for (int i = 0; i < ticks; ++i) {
        const float ta = startAngle + (endAngle - startAngle) * float(i) / float(ticks - 1);
        const bool major = i == 0 || i == ticks - 1 || i == ticks / 2;
        const juce::Point<float> d(std::sin(ta), -std::cos(ta));
        const float r0 = radius - (major ? 5.0f : 3.0f);
        g.setColour(t.label.withAlpha(major ? 0.9f : 0.55f));
        g.drawLine({c + d * r0, c + d * radius}, major ? 1.6f : 1.0f);
    }

    // Value arc just inside the ticks, with a soft glow.
    const float arcR = radius - 8.5f;
    juce::Path track, value;
    track.addCentredArc(c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    value.addCentredArc(c.x, c.y, arcR, arcR, 0.0f, startAngle, a, true);
    g.setColour(t.edge.withAlpha(0.8f));
    g.strokePath(track, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(t.accent.withAlpha(0.22f));
    g.strokePath(value, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(t.accent);
    g.strokePath(value, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Body: a shadow, a dark skirt and a domed cap.
    const float skirtR = (arcR - 3.5f);
    const float capR = skirtR * 0.80f;
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
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawLine({c + d * (capR * 0.18f) + juce::Point<float>(0.0f, 1.0f), c + d * (capR - 2.5f) + juce::Point<float>(0.0f, 1.0f)}, 3.2f);
    g.setColour(t.pointer);
    g.drawLine({c + d * (capR * 0.18f), c + d * (capR - 2.5f)}, 2.4f);
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

// ------------------------------------------------------------------ knob

KnobWithLabel::KnobWithLabel(juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& caption)
    : attachment_(apvts, paramId, slider_) {
    slider_.setRotaryParameters(juce::degreesToRadians(225.0f), juce::degreesToRadians(495.0f), true);
    slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 14);
    slider_.setDoubleClickReturnValue(true, apvts.getParameter(paramId)->convertFrom0to1(apvts.getParameter(paramId)->getDefaultValue()));
    slider_.setMouseDragSensitivity(160);
    slider_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(slider_);
    caption_.setText(caption, juce::dontSendNotification);
    caption_.setJustificationType(juce::Justification::centred);
    caption_.setFont(AmpLookAndFeel::font(10.5f, true, 0.1f));
    addAndMakeVisible(caption_);
}

void KnobWithLabel::lookAndFeelChanged() {
    caption_.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId));
}

void KnobWithLabel::resized() {
    auto r = getLocalBounds();
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
        g.drawText("no impulse loaded", b, juce::Justification::centred);
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
        g.drawText(active_ ? "play a note" : "press  Tuner on  to start", b.withTrimmedBottom(40.0f), juce::Justification::centred);
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
    g.drawText("FLAT", juce::Rectangle<float>(meter.getX(), meter.getBottom() + 4.0f, 60.0f, 14.0f), juce::Justification::centredLeft);
    g.drawText("SHARP", juce::Rectangle<float>(meter.getRight() - 60.0f, meter.getBottom() + 4.0f, 60.0f, 14.0f), juce::Justification::centredRight);
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
    if (count == count_ && name == name_) return;
    count_ = std::max(1, count);
    name_ = name;
    repaint();
}

void TubeGlow::setGlow(float level, float mA) {
    level_ = level;
    mA_ = mA;
    shown_ += 0.25f * (level_ - shown_);  // the glow follows the current with a little inertia, like a hot plate
    repaint();
}

void TubeGlow::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    auto text = r.removeFromBottom(16.0f);
    const float cell = r.getWidth() / float(count_);
    const float w = std::min(30.0f, cell - 8.0f), h = std::min(r.getHeight() - 2.0f, 62.0f);
    const float glow = juce::jlimit(0.0f, 1.0f, shown_ * 1.1f);
    for (int i = 0; i < count_; ++i) {
        const float cx = r.getX() + cell * (float(i) + 0.5f);
        const auto env = juce::Rectangle<float>(cx - w * 0.5f, r.getBottom() - h, w, h - 8.0f);
        juce::Path glass;
        glass.addRoundedRectangle(env.getX(), env.getY(), env.getWidth(), env.getHeight(), w * 0.5f, w * 0.5f, true, true, false, false);

        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillPath(glass);
        {
            // Everything that glows is clipped to the glass.
            juce::Graphics::ScopedSaveState state(g);
            g.reduceClipRegion(glass);
            const auto plate = juce::Rectangle<float>(cx - w * 0.22f, env.getY() + env.getHeight() * 0.22f, w * 0.44f, env.getHeight() * 0.50f);
            const auto hot = juce::Colour::fromRGB(255, 96, 28);
            g.setGradientFill(juce::ColourGradient(hot.withAlpha(0.65f * glow), plate.getCentreX(), plate.getCentreY(), juce::Colours::transparentBlack,
                                                   plate.getCentreX() + w * 0.9f, plate.getCentreY(), true));
            g.fillRect(env);
            g.setColour(juce::Colour::fromRGB(70, 66, 62).interpolatedWith(juce::Colour::fromRGB(255, 120, 40), glow));
            g.fillRoundedRectangle(plate, 1.5f);
            g.setColour(juce::Colours::black.withAlpha(0.35f * (1.0f - glow)));
            g.drawRoundedRectangle(plate, 1.5f, 1.0f);
            // The heater is always lit.
            g.setGradientFill(juce::ColourGradient(juce::Colour::fromRGB(255, 176, 70).withAlpha(0.45f), cx, env.getBottom() - 3.0f, juce::Colours::transparentBlack,
                                                   cx, env.getBottom() - env.getHeight() * 0.35f, true));
            g.fillRect(env);
            // Glass highlight.
            g.setColour(juce::Colours::white.withAlpha(0.10f));
            g.fillRect(env.getX() + 3.0f, env.getY() + 5.0f, 2.0f, env.getHeight() - 10.0f);
        }
        g.setColour(juce::Colours::white.withAlpha(0.28f));
        g.strokePath(glass, juce::PathStrokeType(1.2f));
        // Base with its pins.
        g.setColour(juce::Colour::fromRGB(48, 44, 40));
        g.fillRoundedRectangle(env.getX() - 1.0f, env.getBottom(), w + 2.0f, 6.0f, 1.5f);
        g.setColour(juce::Colour::fromRGB(150, 140, 120));
        g.fillRect(cx - w * 0.3f, env.getBottom() + 6.0f, 1.5f, 2.0f);
        g.fillRect(cx + w * 0.3f - 1.5f, env.getBottom() + 6.0f, 1.5f, 2.0f);
    }
    g.setColour(getLookAndFeel().findColour(juce::Label::textColourId).withAlpha(0.75f));
    g.setFont(AmpLookAndFeel::font(11.0f));
    g.drawText(name_ + (mA_ > 0.5f ? juce::String(" - ") + juce::String(juce::roundToInt(mA_)) + " mA" : juce::String("")), text, juce::Justification::centred);
}

// ------------------------------------------------------------------ editor

SasEditor::SasEditor(SasProcessor& p)
    : AudioProcessorEditor(&p), processor_(p),
      input_(p.apvts, ids::input, "INPUT dB"), output_(p.apvts, ids::output, "OUTPUT dB"),
      tsDrive_(p.apvts, ids::tsDrive, "DRIVE"), tsTone_(p.apvts, ids::tsTone, "TONE"), tsLevel_(p.apvts, ids::tsLevel, "LEVEL"),
      gateThreshold_(p.apvts, ids::gateThreshold, "THRESHOLD"), gateRelease_(p.apvts, ids::gateRelease, "RELEASE"), cable_(p.apvts, ids::cable, "CABLE m"),
      bias_(p.apvts, ids::bias, "BIAS"), wear_(p.apvts, ids::wear, "WEAR"), speaker_(p.apvts, ids::speaker, "DYNAMICS"), blend_(p.apvts, ids::irBlend, "BLEND"),
      compSustain_(p.apvts, ids::compSustain, "SUSTAIN"), compBlend_(p.apvts, ids::compBlend, "BLEND"), compTone_(p.apvts, ids::compTone, "TONE"),
      compLevel_(p.apvts, ids::compLevel, "LEVEL"), tunerRef_(p.apvts, ids::tunerRef, "REF Hz"),
      stereoWidth_(p.apvts, ids::irWidth, "WIDTH"), doubleAmount_(p.apvts, ids::doubleAmount, "DOUBLE"), doubleTime_(p.apvts, ids::doubleTime, "TIME"), pitchFine_(p.apvts, ids::pitchFine, "FINE cents"), eqHpf_(p.apvts, ids::eqHpf, "HIGH-PASS"), eqLpf_(p.apvts, ids::eqLpf, "LOW-PASS"), excLow_(p.apvts, ids::excLow, "BASS"), excHigh_(p.apvts, ids::excHigh, "TREBLE"), eqCurve_(p.apvts) {
    setLookAndFeel(&look_);
    for (auto* k : {&input_, &output_, &tsDrive_, &tsTone_, &tsLevel_, &gateThreshold_, &gateRelease_, &cable_, &bias_, &wear_, &speaker_, &blend_, &compSustain_, &compBlend_, &compTone_, &compLevel_, &tunerRef_, &stereoWidth_, &doubleAmount_, &doubleTime_}) addAndMakeVisible(*k);
    addAndMakeVisible(inBar_);
    addAndMakeVisible(tubes_);
    addAndMakeVisible(outBar_);

    if (const auto* amp = dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter(ids::amp))) ampNames_ = amp->choices;
    ampList_.setModel(this);
    ampList_.setRowHeight(26);
    addAndMakeVisible(ampList_);
    ampSearch_.setTextToShowWhenEmpty("Search amps", look_.text().withAlpha(0.45f));
    ampSearch_.setFont(AmpLookAndFeel::font(13.0f, false, 0.01f));
    ampSearch_.setMultiLine(false);
    ampSearch_.setReturnKeyStartsNewLine(false);
    ampSearch_.setEscapeAndReturnKeysConsumed(true);
    ampSearch_.setIndents(8, 0);
    ampSearch_.onTextChange = [this] { refreshAmpList(); };
    ampSearch_.onEscapeKey = [this] { ampSearch_.clear(); refreshAmpList(); };
    addAndMakeVisible(ampSearch_);
    refreshAmpList();

    styleCombo(quality_, ids::quality, qualityAtt_);
    styleCombo(cab_, ids::cab, cabAtt_);
    styleCombo(pickup_, ids::pickup, pickupAtt_);
    styleCaption(pickupCaption_, "PICKUP");
    styleCombo(noise_, ids::noise, noiseAtt_);
    styleCombo(unit_, ids::unit, unitAtt_);
    styleCaption(noiseCaption_, "NOISE AND HUM");
    styleCaption(unitCaption_, "UNIT");
    styleCaption(tubeCaption_, "POWER TUBES");
    addAndMakeVisible(tube_);
    tube_.onChange = [this] {
        if (auto* param = processor_.apvts.getParameter(ids::tube)) {
            const int index = tube_.getSelectedItemIndex();
            if (index < 0 || shownAmp_ < 0) return;
            // The amp's own tube is "Stock"; the others are stored by type (see wantedCondition).
            const auto& tubes = processor_.amps()[std::size_t(shownAmp_)].power.tubes;
            int type = 0;
            if (index > 0 && index < int(tubes.size()))
                for (int t = 1; t < 4; ++t)
                    if (tubes[std::size_t(index)].name == ids::tubeTypes[t]) type = t;
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->convertTo0to1(float(type)));
            param->endChangeGesture();
        }
    };
    idleLabel_.setFont(AmpLookAndFeel::font(11.5f));
    addAndMakeVisible(idleLabel_);
    conditionNote_.setText("Tubes, wear and unit rebuild the circuits; they act in Normal and High quality.", juce::dontSendNotification);
    conditionNote_.setFont(AmpLookAndFeel::font(10.5f));
    addAndMakeVisible(conditionNote_);
    // The jack list depends on the amp, so it is filled by showAmp() and wired by hand.
    addAndMakeVisible(jack_);
    jack_.onChange = [this] {
        if (auto* param = processor_.apvts.getParameter(ids::jack)) {
            const int index = jack_.getSelectedItemIndex();
            if (index < 0) return;
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->convertTo0to1(float(index)));
            param->endChangeGesture();
        }
    };
    styleCaption(ampCaption_, "AMP");
    styleCaption(qualityCaption_, "QUALITY");
    styleCaption(jackCaption_, "INPUT JACK");
    styleCaption(cabCaption_, "SOURCE");
    addAndMakeVisible(tsOn_);
    tsOnAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::tsOn, tsOn_);
    addAndMakeVisible(gateOn_);
    gateOnAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::gateOn, gateOn_);
    addAndMakeVisible(tunerDisplay_);
    addAndMakeVisible(tunerOn_);
    tunerOnAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::tunerOn, tunerOn_);
    addAndMakeVisible(tunerMute_);
    tunerMuteAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::tunerMute, tunerMute_);
    addAndMakeVisible(compOn_);
    compOnAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::compOn, compOn_);
    addAndMakeVisible(compFast_);
    compFast_.setTooltip("Fast release: the pedal's Attack switch (the envelope recovers through 10k instead of 160k)");
    compFastAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::compFast, compFast_);
    addAndMakeVisible(speakerIntense_);
    speakerIntense_.setTooltip("Stronger cone, heating and break-up effects");
    speakerIntenseAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::speakerIntense, speakerIntense_);
    addAndMakeVisible(vibrato_);
    vibratoAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::vibOn, vibrato_);
    addAndMakeVisible(levelMatch_);
    levelMatch_.setTooltip("Raises the quieter jack so both play at a similar level");
    levelMatchAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::levelMatch, levelMatch_);

    for (auto* l : {&nameA_, &nameB_}) {
        l->setFont(AmpLookAndFeel::font(11.5f));
        addAndMakeVisible(*l);
    }
    blendNote_.setFont(AmpLookAndFeel::font(10.5f));
    blendNote_.setJustificationType(juce::Justification::centred);
    blendNote_.setText("A  <  >  B", juce::dontSendNotification);
    addAndMakeVisible(blendNote_);
    cabNote_.setFont(AmpLookAndFeel::font(10.5f));
    cabNote_.setText("IR plays the impulses above, mixed by the blend. Filter is a simple cabinet curve; Off is the raw amp.", juce::dontSendNotification);
    addAndMakeVisible(cabNote_);
    for (auto* b : {&loadA_, &clearA_, &loadB_, &clearB_}) addAndMakeVisible(*b);
    addAndMakeVisible(curveA_);
    addAndMakeVisible(curveB_);
    loadA_.onClick = [this] { chooseImpulseResponse(0); };
    loadB_.onClick = [this] { chooseImpulseResponse(1); };
    clearA_.onClick = [this] { processor_.clearImpulseResponse(0); };
    clearB_.onClick = [this] { processor_.clearImpulseResponse(1); };

    addAndMakeVisible(pitchFine_);
    for (auto* c : std::initializer_list<juce::Component*>{&eqHpf_, &eqLpf_, &eqCurve_, &eqOn_, &eqReset_, &eqBandLabel_, &excLow_, &excHigh_}) addAndMakeVisible(*c);
    eqOnAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::eqOn, eqOn_);
    for (int b = 0; b < 5; ++b) {
        eqKnobs_[b][0] = std::make_unique<KnobWithLabel>(p.apvts, ids::eqFreq[b], "FREQ Hz");
        eqKnobs_[b][1] = std::make_unique<KnobWithLabel>(p.apvts, ids::eqGain[b], "GAIN dB");
        eqKnobs_[b][2] = std::make_unique<KnobWithLabel>(p.apvts, ids::eqQ[b], "Q");
        for (auto& k : eqKnobs_[b]) addChildComponent(*k);
    }
    eqBandLabel_.setJustificationType(juce::Justification::centredLeft);
    eqBandLabel_.setFont(AmpLookAndFeel::font(13.0f, true, 0.05f));
    eqBandLabel_.setColour(juce::Label::textColourId, look_.text().withAlpha(0.85f));
    eqCurve_.onSelect = [this](int band) { selectEqBand(band); };
    eqReset_.onClick = [this] {
        for (auto* id : {ids::eqHpf, ids::eqLpf})
            if (auto* q = processor_.apvts.getParameter(id)) { q->beginChangeGesture(); q->setValueNotifyingHost(q->getDefaultValue()); q->endChangeGesture(); }
        for (int b = 0; b < 5; ++b)
            for (auto* id : {ids::eqFreq[b], ids::eqGain[b], ids::eqQ[b]})
                if (auto* q = processor_.apvts.getParameter(id)) { q->beginChangeGesture(); q->setValueNotifyingHost(q->getDefaultValue()); q->endChangeGesture(); }
    };
    selectEqBand(2);
    addAndMakeVisible(presets_);
    presets_.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible(pitchSlider_);
    pitchSlider_.setRange(-12.0, 12.0, 1.0);
    pitchSlider_.setDoubleClickReturnValue(true, 0.0);
    pitchAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, ids::pitch, pitchSlider_);
    pitchSlider_.onValueChange = [this] { updatePitchLabels(); };
    pitchValue_.setJustificationType(juce::Justification::centred);
    pitchValue_.setFont(AmpLookAndFeel::font(44.0f, true, 0.01f));
    pitchValue_.setColour(juce::Label::textColourId, look_.accent());
    pitchNote_.setJustificationType(juce::Justification::centred);
    pitchNote_.setFont(AmpLookAndFeel::font(13.0f, false, 0.01f));
    pitchNote_.setColour(juce::Label::textColourId, look_.text().withAlpha(0.6f));
    for (auto* c : std::initializer_list<juce::Component*>{&pitchValue_, &pitchNote_}) addAndMakeVisible(*c);
    for (auto* b : {&pitchDown12_, &pitchDown1_, &pitchUp1_, &pitchUp12_, &pitchZero_}) addAndMakeVisible(*b);
    pitchDown12_.onClick = [this] { pitchSlider_.setValue(-12.0); };
    pitchDown1_.onClick = [this] { pitchSlider_.setValue(pitchSlider_.getValue() - 1.0); };
    pitchUp1_.onClick = [this] { pitchSlider_.setValue(pitchSlider_.getValue() + 1.0); };
    pitchUp12_.onClick = [this] { pitchSlider_.setValue(12.0); };
    pitchZero_.onClick = [this] { pitchSlider_.setValue(0.0); };
    updatePitchLabels();

    for (auto* t : {&ampTab_, &cabTab_, &fxTab_, &pitchTab_, &eqTab_, &tunerTab_}) {
        t->setClickingTogglesState(true);
        t->setRadioGroupId(7);
        addAndMakeVisible(*t);
    }
    ampTab_.setToggleState(true, juce::dontSendNotification);
    ampTab_.onClick = [this] { setPage(0); };
    fxTab_.onClick = [this] { setPage(1); };
    cabTab_.onClick = [this] { setPage(2); };
    tunerTab_.onClick = [this] { setPage(3); };
    pitchTab_.onClick = [this] { setPage(4); };
    eqTab_.onClick = [this] { setPage(5); };

    showAmp(processor_.selectedAmpIndex());
    setResizable(true, true);
    setResizeLimits(680, 380, 1200, 760);
    setSize(800, 410);
    startTimerHz(30);
    timerCallback();
}

SasEditor::~SasEditor() {
    ampList_.setModel(nullptr);
    setLookAndFeel(nullptr);
}

// ------------------------------------------------------------------ amp list

int SasEditor::getNumRows() { return int(shown_.size()); }

void SasEditor::refreshAmpList() {
    const auto query = ampSearch_.getText().trim();
    shown_.clear();
    for (int i = 0; i < ampNames_.size(); ++i)
        if (query.isEmpty() || ampNames_[i].containsIgnoreCase(query)) shown_.push_back(i);
    ampList_.updateContent();
    juce::SparseSet<int> sel;
    const auto it = std::find(shown_.begin(), shown_.end(), processor_.selectedAmpIndex());
    if (it != shown_.end()) {
        const int row = int(it - shown_.begin());
        sel.addRange(juce::Range<int>(row, row + 1));
        ampList_.setSelectedRows(sel, juce::dontSendNotification);
        ampList_.scrollToEnsureRowIsOnscreen(row);
    } else {
        ampList_.setSelectedRows(sel, juce::dontSendNotification);
    }
    ampList_.repaint();
}

void SasEditor::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
    if (row < 0 || row >= int(shown_.size())) return;
    if (selected) {
        g.setGradientFill(juce::ColourGradient(look_.accent().withAlpha(0.26f), 0.0f, 0.0f, look_.accent().withAlpha(0.06f), float(w), 0.0f, false));
        g.fillRect(0, 0, w, h);
        g.setColour(look_.accent());
        g.fillRoundedRectangle(0.0f, 4.0f, 3.0f, float(h - 8), 1.5f);
    }
    g.setColour(selected ? look_.accent() : look_.text().withAlpha(0.8f));
    g.setFont(AmpLookAndFeel::font(14.0f, selected, 0.01f));
    g.drawText(ampNames_[shown_[std::size_t(row)]], 12, 0, w - 16, h, juce::Justification::centredLeft);
}

void SasEditor::selectedRowsChanged(int lastRow) {
    if (lastRow < 0 || lastRow >= int(shown_.size())) return;
    const int amp = shown_[std::size_t(lastRow)];
    if (amp == processor_.selectedAmpIndex()) return;
    if (auto* param = processor_.apvts.getParameter(ids::amp)) {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(float(amp)));
        param->endChangeGesture();
    }
}

void SasEditor::styleCombo(juce::ComboBox& box, const char* paramId,
                           std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& att) {
    addAndMakeVisible(box);
    // Items first, then the attachment selects the stored one.
    const auto* param = dynamic_cast<juce::AudioParameterChoice*>(processor_.apvts.getParameter(paramId));
    box.addItemList(param->choices, 1);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor_.apvts, paramId, box);
}

void SasEditor::styleCaption(juce::Label& l, const juce::String& text) {
    l.setText(text, juce::dontSendNotification);
    l.setFont(AmpLookAndFeel::font(10.5f, true, 0.12f));
    l.setColour(juce::Label::textColourId, look_.label());
    addAndMakeVisible(l);
}

void SasEditor::applyTheme(const sas::AmpDef& def) {
    look_.setTheme(EditorTheme::from(def.theme));
    for (auto* l : {&ampCaption_, &qualityCaption_, &jackCaption_, &cabCaption_, &pickupCaption_, &tubeCaption_, &noiseCaption_, &unitCaption_, &conditionNote_})
        l->setColour(juce::Label::textColourId, look_.label());
    for (auto* l : {&nameA_, &nameB_, &blendNote_, &cabNote_}) l->setColour(juce::Label::textColourId, look_.label());
    sendLookAndFeelChange();
    repaint();
}

void SasEditor::selectEqBand(int band) {
    static const char* names[5] = {"LOW SHELF", "LOW MID", "MID", "HIGH MID", "HIGH SHELF"};
    band = juce::jlimit(0, 4, band);
    eqCurve_.select(band);
    eqBandLabel_.setText(juce::String(band + 1) + "  " + names[band], juce::dontSendNotification);
    for (int b = 0; b < 5; ++b)
        for (auto& k : eqKnobs_[b]) k->setVisible(page_ == 5 && b == band);
    repaint();
}

// ------------------------------------------------------------------ equaliser curve

EqCurve::EqCurve(juce::AudioProcessorValueTreeState& apvts) : apvts_(apvts) { setRepaintsOnMouseActivity(true); }

sas::EqParams EqCurve::params() const {
    sas::EqParams p;
    p.on = apvts_.getRawParameterValue(ids::eqOn)->load() > 0.5f;
    p.hpfHz = double(apvts_.getRawParameterValue(ids::eqHpf)->load());
    p.lpfHz = double(apvts_.getRawParameterValue(ids::eqLpf)->load());
    for (int b = 0; b < 5; ++b) {
        p.band[std::size_t(b)].freq = double(apvts_.getRawParameterValue(ids::eqFreq[b])->load());
        p.band[std::size_t(b)].gainDb = double(apvts_.getRawParameterValue(ids::eqGain[b])->load());
        p.band[std::size_t(b)].q = double(apvts_.getRawParameterValue(ids::eqQ[b])->load());
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
    for (int b = 0; b < 5; ++b) {
        const float d = p.getDistanceFrom({xOf(prm.band[std::size_t(b)].freq), yOf(prm.band[std::size_t(b)].gainDb)});
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

void EqCurve::setPlain(const char* id, double value, bool gesture) {
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
        for (auto* id : {ids::eqFreq[hit], ids::eqGain[hit]})
            if (auto* q = apvts_.getParameter(id)) q->beginChangeGesture();
    }
}

void EqCurve::mouseDrag(const juce::MouseEvent& e) {
    if (dragging_ < 0) return;
    const auto fr = apvts_.getParameterRange(ids::eqFreq[dragging_]), gr = apvts_.getParameterRange(ids::eqGain[dragging_]);
    setPlain(ids::eqFreq[dragging_], juce::jlimit(double(fr.start), double(fr.end), hzAt(e.position.x)), false);
    setPlain(ids::eqGain[dragging_], juce::jlimit(double(gr.start), double(gr.end), dbAt(e.position.y)), false);
}

void EqCurve::mouseUp(const juce::MouseEvent&) {
    if (dragging_ < 0) return;
    for (auto* id : {ids::eqFreq[dragging_], ids::eqGain[dragging_]})
        if (auto* q = apvts_.getParameter(id)) q->endChangeGesture();
    dragging_ = -1;
}

void EqCurve::mouseDoubleClick(const juce::MouseEvent& e) {
    const int hit = nodeAt(e.position);
    if (hit >= 0) setPlain(ids::eqGain[hit], 0.0, true);
}

void EqCurve::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    int band = nodeAt(e.position);
    if (band < 0) band = selected_;
    const auto r = apvts_.getParameterRange(ids::eqQ[band]);
    const double q = double(apvts_.getRawParameterValue(ids::eqQ[band])->load()) * std::pow(1.25, double(w.deltaY) * 4.0);
    setPlain(ids::eqQ[band], juce::jlimit(double(r.start), double(r.end), q), true);
}

void EqCurve::paint(juce::Graphics& g) {
    const auto* look = dynamic_cast<AmpLookAndFeel*>(&getLookAndFeel());
    const auto accent = look ? look->accent() : juce::Colours::orange, text = look ? look->text() : juce::Colours::white, edge = look ? look->panelEdge() : juce::Colours::grey;
    const auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.30f));
    g.fillRoundedRectangle(b, 5.0f);
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
    for (int i = 0; i < 5; ++i) {
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

void SasEditor::updatePitchLabels() {
    const int st = int(std::lround(pitchSlider_.getValue()));
    pitchValue_.setText(juce::String(st >= 0 ? "+" : "") + juce::String(st) + " st", juce::dontSendNotification);
    const int cents = int(processor_.apvts.getRawParameterValue(ids::pitchFine)->load() + (processor_.apvts.getRawParameterValue(ids::pitchFine)->load() >= 0 ? 0.5f : -0.5f));
    juce::String text = st == 0 ? juce::String("pitch unchanged") : st == 12 ? juce::String("one octave up") : st == -12 ? juce::String("one octave down")
                                                                       : st > 0 ? juce::String(st) + " semitones up" : juce::String(-st) + " semitones down";
    if (cents != 0) text += juce::String(", ") + (cents > 0 ? "+" : "") + juce::String(cents) + " cents";
    const bool active = st != 0 || cents != 0;
    text += active ? juce::String("   -   adds about 12 ms of delay") : juce::String("   -   off, no delay");
    pitchNote_.setText(text, juce::dontSendNotification);
}

void SasEditor::setPage(int page) {
    page_ = page;
    resized();
}

void SasEditor::showAmp(int index) {
    const auto& defs = processor_.amps();
    if (index < 0 || index >= int(defs.size())) return;
    shownAmp_ = index;
    const auto& def = defs[std::size_t(index)];
    title_ = juce::String(def.name);
    applyTheme(def);

    ampKnobs_.clear();
    for (const auto& u : def.ui) {
        auto k = std::make_unique<KnobWithLabel>(processor_.apvts, u.param.c_str(), u.label);
        addAndMakeVisible(*k);
        ampKnobs_.push_back(std::move(k));
    }

    jack_.clear(juce::dontSendNotification);
    for (std::size_t j = 0; j < def.preamp.jacks.size(); ++j) jack_.addItem(juce::String(def.preamp.jacks[j].label), int(j) + 1);
    jackCaption_.setText(juce::String(def.preamp.jackLabel), juce::dontSendNotification);
    tube_.clear(juce::dontSendNotification);
    for (std::size_t t = 0; t < def.power.tubes.size(); ++t) tube_.addItem(juce::String(def.power.tubes[t].name), int(t) + 1);
    tube_.setSelectedItemIndex(processor_.wantedCondition().tube, juce::dontSendNotification);
    tube_.setEnabled(def.power.tubes.size() > 1);
    bias_.setEnabled(def.power.bias.adjustable());
    bias_.setAlpha(def.power.bias.adjustable() ? 1.0f : 0.4f);
    vibrato_.setVisible(def.preamp.vibrato.present);
    levelMatch_.setVisible(std::any_of(def.preamp.jacks.begin(), def.preamp.jacks.end(), [](const sas::JackDef& j) { return j.levelDb != 0.0; }));
    const int jackNow = int(processor_.apvts.getRawParameterValue(ids::jack)->load() + 0.5f);
    jack_.setSelectedItemIndex(juce::jlimit(0, int(def.preamp.jacks.size()) - 1, jackNow), juce::dontSendNotification);
    refreshAmpList();
    resized();
    repaint();
}

void SasEditor::pollMeters() {
    inBar_.setLevel(processor_.takeInputPeak());
    outBar_.setLevel(processor_.takeOutputPeak());
    if (shownAmp_ >= 0 && shownAmp_ < int(processor_.amps().size())) {
        const auto& def = processor_.amps()[std::size_t(shownAmp_)];
        const int idx = def.power.tubes.empty() ? 0 : std::min(processor_.wantedCondition().tube, int(def.power.tubes.size()) - 1);
        const double pmax = def.power.tubes.empty() ? 25.0 : def.power.tubes[std::size_t(idx)].pmaxW;
        const juce::String name = def.power.tubes.empty() ? juce::String("tubes") : juce::String(def.power.tubes[std::size_t(idx)].name);
        tubes_.setTubes(int(def.power.bias.tubesPerHalf * 2.0 + 0.5), name);
        const float ma = processor_.idleMilliamps();
        tubes_.setGlow(float(ma / (pmax / 0.42)), ma);  // dissipation ~ current x about 420 V of plate voltage
    }
}

void SasEditor::timerCallback() {
    pollMeters();
    if (page_ == 5) eqCurve_.repaint();  // follows the parameters when the host or an automation moves them
    if (++tunerTicks_ % 3 == 0) updateTuner();
    if (tick_++ % 8 != 0) return;  // the rest at about 4 Hz
    // Follows the amp parameter when the host or a restored state changes it.
    if (processor_.selectedAmpIndex() != shownAmp_) showAmp(processor_.selectedAmpIndex());
    const int jackNow = int(processor_.apvts.getRawParameterValue(ids::jack)->load() + 0.5f);
    if (jackNow < jack_.getNumItems() && jack_.getSelectedItemIndex() != jackNow)
        jack_.setSelectedItemIndex(jackNow, juce::dontSendNotification);

    if (const int tubeNow = processor_.wantedCondition().tube;
        tubeNow < tube_.getNumItems() && tube_.getSelectedItemIndex() != tubeNow)
        tube_.setSelectedItemIndex(tubeNow, juce::dontSendNotification);
    {
        const float ma = processor_.idleMilliamps();
        idleLabel_.setText(ma > 0.5f ? juce::String(ma, 1) + " mA per tube" : juce::String("plate current: Normal / High only"), juce::dontSendNotification);
    }

    updateImpulseStatus();
}

void SasEditor::updateTuner() {
    const bool active = processor_.apvts.getRawParameterValue(ids::tunerOn)->load() > 0.5f;
    const double ref = double(processor_.apvts.getRawParameterValue(ids::tunerRef)->load());
    bool valid = false;
    if (active && processor_.tunerFeed().copyLatest(tunerBuf_.data(), tunerBuf_.size())) {
        const auto r = sas::PitchDetector::detect(tunerBuf_.data(), tunerBuf_.size(), processor_.tunerFeed().rate());
        if (r.valid) {
            // Take a jump of more than a semitone only after it repeats, so one bad reading does not flash a wrong note.
            const bool jump = tunerHz_ > 0.0 && std::abs(12.0 * std::log2(r.hz / tunerHz_)) > 1.0;
            if (!jump || ++tunerHold_ >= 2) {
                tunerHz_ = r.hz;
                tunerHold_ = 0;
            }
            valid = true;
        } else {
            tunerHold_ = 0;
        }
    }
    if (!active) tunerHz_ = 0.0;
    if (page_ == 3 || active) tunerDisplay_.setReading(valid && tunerHz_ > 0.0, active, tunerHz_, ref);
}

void SasEditor::updateImpulseStatus() {
    for (int slot = 0; slot < 2; ++slot) {
        const auto name = processor_.impulseResponseName(slot);
        (slot == 0 ? nameA_ : nameB_).setText(name.isEmpty() ? juce::String("empty") : name, juce::dontSendNotification);
        (slot == 0 ? curveA_ : curveB_).setCurve(processor_.impulseResponseCurve(slot));
    }
}

// The Presets button: save the current state, load a file, or pick one of the presets saved in the presets folder.
void SasEditor::showPresetMenu() {
    const auto folder = SasProcessor::presetFolder();
    juce::PopupMenu menu;
    menu.addItem(1, "Save preset...");
    menu.addItem(2, "Load preset file...");
    menu.addItem(3, "Open the presets folder");
    juce::Array<juce::File> files;
    folder.findChildFiles(files, juce::File::findFiles, false, "*.sasp");
    files.sort();
    if (!files.isEmpty()) {
        menu.addSeparator();
        for (int i = 0; i < files.size() && i < 200; ++i) menu.addItem(100 + i, files[i].getFileNameWithoutExtension());
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presets_), [this, folder, files](int choice) {
        auto load = [this](const juce::File& f) {
            if (!processor_.loadPreset(f))
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Schematic Amp Sim", "Could not read that preset.");
        };
        if (choice == 1) {
            folder.createDirectory();
            chooser_ = std::make_unique<juce::FileChooser>("Save the preset", folder.getChildFile("My preset.sasp"), "*.sasp");
            chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this](const juce::FileChooser& fc) {
                                      auto file = fc.getResult();
                                      if (file == juce::File{}) return;
                                      if (!file.hasFileExtension("sasp")) file = file.withFileExtension("sasp");
                                      if (!processor_.savePreset(file))
                                          juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Schematic Amp Sim", "Could not write that file.");
                                  });
        } else if (choice == 2) {
            chooser_ = std::make_unique<juce::FileChooser>("Load a preset", folder, "*.sasp");
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [load](const juce::FileChooser& fc) {
                const auto file = fc.getResult();
                if (file != juce::File{}) load(file);
            });
        } else if (choice == 3) {
            folder.createDirectory();
            folder.revealToUser();
        } else if (choice >= 100 && choice - 100 < files.size()) {
            load(files[choice - 100]);
        }
    });
}

void SasEditor::chooseImpulseResponse(int slot) {
    chooser_ = std::make_unique<juce::FileChooser>("Choose a cabinet impulse response", juce::File{}, "*.wav;*.aif;*.aiff;*.flac");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, slot](const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File{}) return;
                              if (processor_.loadImpulseResponse(file, slot)) {
                                  // Switch the cabinet to IR so the load is audible right away.
                                  if (auto* cab = processor_.apvts.getParameter(ids::cab)) cab->setValueNotifyingHost(cab->convertTo0to1(2.0f));
                              } else {
                                  juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Schematic Amp Sim",
                                                                         "Could not read that file as an impulse response.");
                              }
                          });
}

// The product mark: a triode symbol (envelope, plate, dashed grid, cathode) in the accent colour, and the name beside it.
void SasEditor::drawLogo(juce::Graphics& g, juce::Rectangle<float> r) const {
    const float size = std::min(r.getHeight(), 38.0f);
    const auto c = juce::Point<float>(r.getX() + size * 0.5f + 2.0f, r.getCentreY());
    const float u = size / 38.0f;  // the drawing below is laid out for a 38 px box
    const auto accent = look_.accent();

    g.setColour(accent.withAlpha(0.16f));  // soft glow behind the mark
    g.fillEllipse(c.x - 17.0f * u, c.y - 17.0f * u, 34.0f * u, 34.0f * u);
    g.setColour(accent);
    g.drawEllipse(c.x - 14.0f * u, c.y - 14.0f * u, 28.0f * u, 28.0f * u, 1.8f);
    // Plate with its lead, dashed grid with its lead, cathode with its lead.
    g.drawLine(c.x - 6.0f * u, c.y - 7.0f * u, c.x + 6.0f * u, c.y - 7.0f * u, 2.4f);
    g.drawLine(c.x, c.y - 7.0f * u, c.x, c.y - 17.0f * u, 1.6f);
    for (int i = 0; i < 4; ++i) {
        const float x0 = c.x - 7.0f * u + float(i) * 4.0f * u;
        g.drawLine(x0, c.y, x0 + 2.4f * u, c.y, 1.6f);
    }
    g.drawLine(c.x - 7.0f * u, c.y, c.x - 17.0f * u, c.y, 1.6f);
    juce::Path cathode;
    cathode.startNewSubPath(c.x - 5.0f * u, c.y + 8.0f * u);
    cathode.quadraticTo(c.x, c.y + 3.5f * u, c.x + 5.0f * u, c.y + 8.0f * u);
    g.strokePath(cathode, juce::PathStrokeType(1.8f));
    g.drawLine(c.x, c.y + 6.0f * u, c.x, c.y + 17.0f * u, 1.6f);

    const float tx = c.x + 20.0f * u;
    g.setColour(look_.text());
    g.setFont(AmpLookAndFeel::font(14.0f, true, 0.14f));
    g.drawText("SCHEMATIC", juce::Rectangle<float>(tx, r.getY() + 3.0f, r.getRight() - tx, 16.0f), juce::Justification::centredLeft);
    g.setColour(accent);
    g.setFont(AmpLookAndFeel::font(14.0f, true, 0.30f));
    g.drawText("AMP SIM", juce::Rectangle<float>(tx, r.getY() + 19.0f, r.getRight() - tx, 16.0f), juce::Justification::centredLeft);
}

void SasEditor::paint(juce::Graphics& g) {
    const auto bg = look_.background();
    g.setGradientFill(juce::ColourGradient(bg.brighter(0.18f), 0.0f, 0.0f, bg.darker(0.35f), 0.0f, float(getHeight()), false));
    g.fillAll();
    // A soft glow of the accent colour behind the title.
    g.setGradientFill(juce::ColourGradient(look_.accent().withAlpha(0.07f), float(getWidth()) * 0.35f, 0.0f, juce::Colours::transparentBlack,
                                           float(getWidth()) * 0.35f, 120.0f, true));
    g.fillRect(0, 0, getWidth(), 130);

    for (const auto& p : panels_) {
        const auto b = p.bounds.toFloat();
        const auto top = look_.panel().brighter(0.10f);
        g.setGradientFill(juce::ColourGradient(top, 0.0f, b.getY(), look_.panel().darker(0.15f), 0.0f, b.getBottom(), false));
        g.fillRoundedRectangle(b, 7.0f);
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawHorizontalLine(int(b.getY()) + 1, b.getX() + 8.0f, b.getRight() - 8.0f);
        g.setColour(look_.panelEdge());
        g.drawRoundedRectangle(b.reduced(0.5f), 7.0f, 1.0f);
        if (p.caption.isNotEmpty()) {
            g.setColour(look_.accent());
            g.fillRoundedRectangle(b.getX() + 10.0f, b.getY() + 9.0f, 3.0f, 9.0f, 1.5f);
            g.setColour(look_.label());
            g.setFont(AmpLookAndFeel::font(10.0f, true, 0.07f));
            g.drawText(p.caption, p.bounds.getX() + 18, p.bounds.getY() + 4, p.bounds.getWidth() - 28, 18, juce::Justification::centredLeft);
        }
    }

    drawLogo(g, logoBounds_.toFloat());
    g.setColour(look_.label().withAlpha(0.65f));
    g.setFont(AmpLookAndFeel::font(10.0f));
    g.drawText(juce::String("v") + sas::engineVersion(), 12, getHeight() - 24, 168, 14, juce::Justification::centredLeft);

    // Title of the amp, with a thin accent line under it.
    const int left = 12 + 176;
    g.setColour(look_.accent());
    g.setFont(AmpLookAndFeel::font(26.0f, true, 0.02f));
    g.drawText(title_, left, 12, getWidth() - left - 12 - 160, 34, juce::Justification::centredLeft);
    g.setGradientFill(juce::ColourGradient(look_.accent().withAlpha(0.7f), float(left), 0.0f, look_.accent().withAlpha(0.0f), float(getWidth() - 12), 0.0f, false));
    g.fillRect(float(left), 49.0f, float(getWidth() - 12 - left), 1.0f);
}

void SasEditor::resized() {
    constexpr int pad = 12, gap = 8, leftW = 168;
    panels_.clear();
    auto area = getLocalBounds().reduced(pad);

    // Left column: the amp list, jack, quality.
    {
        auto col = area.removeFromLeft(leftW);
        area.removeFromLeft(gap);
        logoBounds_ = col.removeFromTop(40);
        col.removeFromTop(10);
        ampCaption_.setVisible(false);  // the logo and the list say what this is
        ampSearch_.setBounds(col.removeFromTop(24));
        col.removeFromTop(4);
        ampList_.setBounds(col.removeFromTop(5 * 26 + 14));  // five rows and a bit of the next: it scrolls
        col.removeFromTop(gap);
        jackCaption_.setBounds(col.removeFromTop(16));
        jack_.setBounds(col.removeFromTop(24));
        col.removeFromTop(4);
        if (levelMatch_.isVisible()) levelMatch_.setBounds(col.removeFromTop(22));
        if (vibrato_.isVisible()) vibrato_.setBounds(col.removeFromTop(22));
        col.removeFromTop(4);
        qualityCaption_.setBounds(col.removeFromTop(16));
        quality_.setBounds(col.removeFromTop(24));
        col.removeFromTop(6);
        presets_.setBounds(col.removeFromTop(24));
    }

    // Right: title and page tabs, then the page.
    {
        auto head = area.removeFromTop(36);
        tunerTab_.setBounds(head.removeFromRight(58).withTrimmedTop(6).withTrimmedBottom(2));
        head.removeFromRight(3);
        eqTab_.setBounds(head.removeFromRight(42).withTrimmedTop(6).withTrimmedBottom(2));
        head.removeFromRight(3);
        pitchTab_.setBounds(head.removeFromRight(54).withTrimmedTop(6).withTrimmedBottom(2));
        head.removeFromRight(3);
        fxTab_.setBounds(head.removeFromRight(70).withTrimmedTop(6).withTrimmedBottom(2));
        head.removeFromRight(4);
        cabTab_.setBounds(head.removeFromRight(74).withTrimmedTop(6).withTrimmedBottom(2));
        head.removeFromRight(4);
        ampTab_.setBounds(head.removeFromRight(54).withTrimmedTop(6).withTrimmedBottom(2));
        area.removeFromTop(4);
    }
    const bool ampPage = page_ == 0, fxPage = page_ == 1, cabPage = page_ == 2, tunerPage = page_ == 3, pitchPage = page_ == 4, eqPage = page_ == 5;
    for (auto* c : std::initializer_list<juce::Component*>{&eqCurve_, &eqOn_, &eqReset_, &eqBandLabel_, &eqHpf_, &eqLpf_, &excLow_, &excHigh_}) c->setVisible(eqPage);
    for (int b = 0; b < 5; ++b)
        for (auto& k : eqKnobs_[b]) k->setVisible(eqPage && b == eqCurve_.selected());
    for (auto* c : std::initializer_list<juce::Component*>{&pitchSlider_, &pitchValue_, &pitchNote_, &pitchDown12_, &pitchDown1_, &pitchUp1_, &pitchUp12_, &pitchZero_, &pitchFine_}) c->setVisible(pitchPage);
    for (auto* c : std::initializer_list<juce::Component*>{&tunerDisplay_, &tunerOn_, &tunerMute_, &tunerRef_}) c->setVisible(tunerPage);
    for (auto& k : ampKnobs_) k->setVisible(ampPage);
    for (auto* c : std::initializer_list<juce::Component*>{&input_, &output_, &inBar_, &outBar_, &tubes_, &cable_, &pickup_, &pickupCaption_}) c->setVisible(ampPage);
    for (auto* c : std::initializer_list<juce::Component*>{&cabCaption_, &cab_, &speaker_, &speakerIntense_, &blend_, &blendNote_, &loadA_, &clearA_, &nameA_, &curveA_,
                                                           &loadB_, &clearB_, &nameB_, &curveB_, &cabNote_, &stereoWidth_, &doubleAmount_, &doubleTime_}) c->setVisible(cabPage);
    for (auto* c : std::initializer_list<juce::Component*>{&compOn_, &compFast_, &compSustain_, &compBlend_, &compTone_, &compLevel_, &tsOn_, &tsDrive_, &tsTone_, &tsLevel_, &gateOn_, &gateThreshold_, &gateRelease_,
                                                           &bias_, &wear_, &tube_, &noise_, &unit_, &tubeCaption_, &noiseCaption_, &unitCaption_, &idleLabel_, &conditionNote_}) c->setVisible(fxPage);

    if (fxPage) {
        // Effects page, in signal order: compressor, overdrive, noise gate; below them the state of the amp itself.
        const int knobW = std::clamp((area.getWidth() - 2 * gap - 36) / 9, 50, 110);
        const int condKnobW = 75;
        auto fxRow = area.removeFromTop(std::min(area.getHeight(), 160));
        auto compPanel = fxRow.removeFromLeft(knobW * 4 + 12);
        fxRow.removeFromLeft(gap);
        auto tsPanel = fxRow.removeFromLeft(knobW * 3 + 12);
        fxRow.removeFromLeft(gap);
        auto gatePanel = fxRow.removeFromLeft(knobW * 2 + 12);
        panels_.push_back({compPanel, "COMPRESSOR"});
        {
            auto cin = compPanel.reduced(6, 4);
            auto chead = cin.removeFromTop(20);
            compOn_.setBounds(chead.removeFromRight(56));
            compFast_.setBounds(chead.removeFromRight(62));
            const int w = cin.getWidth() / 4;
            compSustain_.setBounds(cin.removeFromLeft(w));
            compBlend_.setBounds(cin.removeFromLeft(w));
            compTone_.setBounds(cin.removeFromLeft(w));
            compLevel_.setBounds(cin);
        }
        panels_.push_back({gatePanel, "GATE"});
        {
            auto gin = gatePanel.reduced(6, 4);
            auto ghead = gin.removeFromTop(20);
            gateOn_.setBounds(ghead.removeFromRight(56));
            gateThreshold_.setBounds(gin.removeFromLeft(gin.getWidth() / 2));
            gateRelease_.setBounds(gin);
        }
        // Second row: the state of the amp itself.
        area.removeFromTop(gap);
        auto condPanel = area.removeFromTop(std::min(area.getHeight(), 150));
        panels_.push_back({condPanel, "AMP CONDITION"});
        {
            auto cin = condPanel.reduced(8, 4);
            cin.removeFromTop(18);
            conditionNote_.setBounds(cin.removeFromBottom(16));
            auto col1 = cin.removeFromLeft(130);
            tubeCaption_.setBounds(col1.removeFromTop(16));
            tube_.setBounds(col1.removeFromTop(24));
            idleLabel_.setBounds(col1.removeFromTop(20));
            bias_.setBounds(cin.removeFromLeft(condKnobW));
            auto col2 = cin.removeFromLeft(120);
            col2.removeFromLeft(10);
            noiseCaption_.setBounds(col2.removeFromTop(16));
            noise_.setBounds(col2.removeFromTop(24));
            wear_.setBounds(cin.removeFromLeft(condKnobW));
            auto col3 = cin.removeFromLeft(110);
            unitCaption_.setBounds(col3.removeFromTop(16));
            unit_.setBounds(col3.removeFromTop(24));
        }
        panels_.push_back({tsPanel, "OVERDRIVE - TS808"});
        auto in = tsPanel.reduced(6, 4);
        auto head = in.removeFromTop(20);
        tsOn_.setBounds(head.removeFromRight(56));
        tsDrive_.setBounds(in.removeFromLeft(in.getWidth() / 3));
        tsTone_.setBounds(in.removeFromLeft(in.getWidth() / 2));
        tsLevel_.setBounds(in);
        repaint();
        return;
    }

    if (eqPage) {
        // EQ page: the curve on top; below, the two filters, the selected band's three knobs, and the switches.
        auto bottom = area.removeFromBottom(std::min(110, area.getHeight() / 2));
        area.removeFromBottom(gap);
        auto maxi = area.removeFromRight(104);
        area.removeFromRight(gap);
        panels_.push_back({area, "EQUALISER - after the cabinet"});
        eqCurve_.setBounds(area.reduced(8, 4).withTrimmedTop(16));
        panels_.push_back({maxi, "MAXIMIZER"});
        {
            auto in = maxi.reduced(6, 4);
            in.removeFromTop(16);
            excLow_.setBounds(in.removeFromTop(in.getHeight() / 2));
            excHigh_.setBounds(in);
        }
        auto filters = bottom.removeFromLeft(150);
        bottom.removeFromLeft(gap);
        auto switches = bottom.removeFromRight(86);
        bottom.removeFromRight(gap);
        panels_.push_back({filters, "FILTERS"});
        {
            auto in = filters.reduced(6, 4);
            in.removeFromTop(16);
            eqHpf_.setBounds(in.removeFromLeft(in.getWidth() / 2));
            eqLpf_.setBounds(in);
        }
        panels_.push_back({bottom, "BAND"});
        {
            auto in = bottom.reduced(6, 4);
            in.removeFromTop(16);
            eqBandLabel_.setBounds(in.removeFromLeft(86).withTrimmedLeft(4));
            const int w = in.getWidth() / 3;
            for (int b = 0; b < 5; ++b) {
                auto r = in;
                eqKnobs_[b][0]->setBounds(r.removeFromLeft(w));
                eqKnobs_[b][1]->setBounds(r.removeFromLeft(w));
                eqKnobs_[b][2]->setBounds(r);
            }
        }
        panels_.push_back({switches, "EQ"});
        {
            auto in = switches.reduced(8, 6);
            in.removeFromTop(20);
            eqOn_.setBounds(in.removeFromTop(26));
            in.removeFromTop(8);
            eqReset_.setBounds(in.removeFromTop(26));
        }
        repaint();
        return;
    }

    if (pitchPage) {
        // Pitch page: the shift in semitones (big readout, step buttons and a slider) and, on the right, the fine tuning in cents.
        auto side = area.removeFromRight(130);
        area.removeFromRight(gap);
        panels_.push_back({area, "PITCH SHIFT"});
        panels_.push_back({side, "FINE TUNING"});
        pitchFine_.setBounds(side.reduced(6, 4).withTrimmedTop(16));
        auto in = area.reduced(14, 8);
        in.removeFromTop(16);
        pitchValue_.setBounds(in.removeFromTop(58));
        pitchNote_.setBounds(in.removeFromTop(20));
        in.removeFromTop(10);
        auto buttons = in.removeFromTop(30);
        const int bw = 62, bgap = 8, total = bw * 5 + bgap * 4;
        buttons = buttons.withSizeKeepingCentre(std::min(total, buttons.getWidth()), 30);
        for (auto* b : {&pitchDown12_, &pitchDown1_, &pitchZero_, &pitchUp1_, &pitchUp12_}) {
            b->setBounds(buttons.removeFromLeft(bw));
            buttons.removeFromLeft(bgap);
        }
        in.removeFromTop(14);
        pitchSlider_.setBounds(in.removeFromTop(26));
        repaint();
        return;
    }

    if (tunerPage) {
        // Tuner page: the display fills the page, the switches and the reference pitch sit in a column on the right.
        auto side = area.removeFromRight(150);
        area.removeFromRight(gap);
        panels_.push_back({area, "CHROMATIC TUNER"});
        tunerDisplay_.setBounds(area.reduced(10, 8).withTrimmedTop(14));
        panels_.push_back({side, "TUNER"});
        auto in = side.reduced(10, 6);
        in.removeFromTop(20);
        tunerOn_.setBounds(in.removeFromTop(26));
        in.removeFromTop(4);
        tunerMute_.setBounds(in.removeFromTop(26));
        in.removeFromTop(4);
        tunerRef_.setBounds(in);
        repaint();
        return;
    }

    if (cabPage) {
        // Cabinet page: the two impulse slots on top, each with its response curve; below, the cabinet mode, the A / B blend and the speaker.
        auto top = area.removeFromTop(std::max(150, area.getHeight() * 52 / 100));
        area.removeFromTop(gap);
        auto slotA = top.removeFromLeft((top.getWidth() - gap) / 2);
        top.removeFromLeft(gap);
        auto slotB = top;
        auto slot = [&](juce::Rectangle<int> panel, const char* caption, juce::TextButton& load, juce::TextButton& clear, juce::Label& name, IrCurve& curve) {
            panels_.push_back({panel, caption});
            auto in = panel.reduced(10, 6);
            in.removeFromTop(18);
            auto row = in.removeFromTop(24);
            clear.setBounds(row.removeFromRight(54));
            row.removeFromRight(6);
            load.setBounds(row.removeFromRight(70));
            row.removeFromRight(6);
            name.setBounds(row);
            in.removeFromTop(6);
            curve.setBounds(in);
        };
        slot(slotA, "IMPULSE A", loadA_, clearA_, nameA_, curveA_);
        slot(slotB, "IMPULSE B", loadB_, clearB_, nameB_, curveB_);

        // Bottom row: where the sound comes from, the A / B blend, the speaker, and the stereo.
        auto cabPanel = area.removeFromLeft(140);
        area.removeFromLeft(gap);
        auto blendPanel = area.removeFromLeft(96);
        area.removeFromLeft(gap);
        auto speakerPanel = area.removeFromLeft(118);
        area.removeFromLeft(gap);
        auto stereoPanel = area;
        panels_.push_back({cabPanel, "CABINET"});
        {
            auto in = cabPanel.reduced(10, 6);
            in.removeFromTop(20);
            cabCaption_.setBounds(in.removeFromTop(16));
            cab_.setBounds(in.removeFromTop(24));
            in.removeFromTop(8);
            cabNote_.setBounds(in);
        }
        panels_.push_back({blendPanel, "A / B BLEND"});
        {
            auto in = blendPanel.reduced(6, 4);
            in.removeFromTop(16);
            blendNote_.setBounds(in.removeFromBottom(14));
            blend_.setBounds(in);
        }
        panels_.push_back({speakerPanel, "SPEAKER"});
        {
            auto in = speakerPanel.reduced(6, 4);
            in.removeFromTop(16);
            speakerIntense_.setBounds(in.removeFromBottom(20).withSizeKeepingCentre(80, 20));
            speaker_.setBounds(in);
        }
        panels_.push_back({stereoPanel, "STEREO"});
        {
            auto in = stereoPanel.reduced(6, 4);
            in.removeFromTop(16);
            const int w = in.getWidth() / 3;
            stereoWidth_.setBounds(in.removeFromLeft(w));
            doubleAmount_.setBounds(in.removeFromLeft(w));
            doubleTime_.setBounds(in);
        }
        repaint();
        return;
    }

    const int bottomH = std::clamp(area.getHeight() * 38 / 100, 110, 160);
    auto bottom = area.removeFromBottom(bottomH);
    area.removeFromBottom(gap);

    panels_.push_back({area, "AMP CONTROLS"});
    {
        auto row = area.reduced(6, 4);
        row.removeFromTop(16);
        const int n = std::max<int>(1, int(ampKnobs_.size()));
        const int w = row.getWidth() / n;
        for (auto& k : ampKnobs_) k->setBounds(row.removeFromLeft(w));
    }

    const int knobW = std::clamp(bottom.getWidth() / 7, 64, 100);
    auto ioPanel = bottom.removeFromRight(knobW * 2 + 12 + 2 * 16);
    bottom.removeFromRight(gap);
    // The guitar (pickup and cable) is where the signal starts, so it sits with the amp.
    auto guitarPanel = bottom.removeFromLeft(std::clamp(bottom.getWidth() * 46 / 100, 176, 210));
    bottom.removeFromLeft(gap);
    panels_.push_back({guitarPanel, "GUITAR INPUT"});
    {
        auto gin = guitarPanel.reduced(8, 4);
        gin.removeFromTop(18);
        cable_.setBounds(gin.removeFromRight(70));
        gin.removeFromRight(4);
        pickupCaption_.setBounds(gin.removeFromTop(16));
        pickup_.setBounds(gin.removeFromTop(24));
    }
    auto tubesPanel = bottom;
    panels_.push_back({tubesPanel, "POWER TUBES"});
    tubes_.setBounds(tubesPanel.reduced(8, 4).withTrimmedTop(18));
    panels_.push_back({ioPanel, "LEVELS"});
    {
        auto in = ioPanel.reduced(6, 4);
        in.removeFromTop(16);
        auto col = [&](KnobWithLabel& k, LevelBar& bar, juce::Rectangle<int> r) {
            bar.setBounds(r.removeFromRight(12).withTrimmedTop(20).withTrimmedBottom(20));
            k.setBounds(r);
        };
        col(input_, inBar_, in.removeFromLeft(in.getWidth() / 2));
        col(output_, outBar_, in);
    }
    repaint();
}
