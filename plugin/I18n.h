#pragma once
// The language of the interface. The texts in the source are in English; `tr("...")` gives the same text in the language in use, or the English one
// when there is no translation (so a missing entry shows English, never an empty label). The language is chosen by the player (Auto, English,
// Spanish, Portuguese, Italian, Chinese, French, German, Japanese, Russian or Korean, saved in the user's application data folder, shared by the plugin
// and the standalone); Auto follows the language of the system.
// It is read when an editor is built: a change shows the next time the editor window is opened. Parameter names (what the host shows for
// automation) stay in English, and so do the names of the amps and cabinets.

#include <juce_core/juce_core.h>

namespace i18n {

enum class Language { Auto = 0, English, Spanish, Portuguese, Italian, Chinese, French, German, Japanese, Russian, Korean };

Language preference();            // what the player chose (Auto until they choose)
void setPreference(Language lang);  // saves it and applies it to the texts from now on
// Uses a language for this run only, without saving it (the tests, and what a screenshot needs).
void useForSession(Language lang);
Language active();                // Auto resolved: any but Auto
juce::String languageName(Language lang);  // "Auto", "English", "Español", "Português", "Italiano", "简体中文", "Français", "Deutsch", "日本語", "Русский", "한국어": each in its own language
// The language code of a system language ("es", "es_AR", "pt-BR", "it", "zh_CN", "fr_FR", "de-AT"...) as the interface knows it: Spanish, Portuguese,
// Italian, Chinese, French, German, Japanese, Russian or Korean for any variant of them, English for the rest.
Language fromSystemCode(const juce::String& code);

juce::String lookup(const juce::String& english);
// How many texts a language has translated (0 for English): the tests check that every language covers the same ones.
std::size_t translationCount(Language lang);

// The message of a failed or doubtful model load ("the file is too big", "Trained at 44100 Hz, played at 48000 Hz..."): the processor keeps it in English.
juce::String namMessage(const juce::String& english);

}  // namespace i18n

inline juce::String tr(const juce::String& english) { return i18n::lookup(english); }
// Every text of a list (the items of a choice), each in the language in use; the order is kept.
inline juce::StringArray trAll(const juce::StringArray& english) {
    juce::StringArray out;
    for (const auto& s : english) out.add(tr(s));
    return out;
}
