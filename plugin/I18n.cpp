#include "I18n.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace i18n {

const std::unordered_map<std::string, std::string>& spanishTable();     // I18nEs.cpp
const std::unordered_map<std::string, std::string>& portugueseTable();  // I18nPt.cpp
const std::unordered_map<std::string, std::string>& italianTable();     // I18nIt.cpp
const std::unordered_map<std::string, std::string>& chineseTable();     // I18nZh.cpp
const std::unordered_map<std::string, std::string>& frenchTable();      // I18nFr.cpp
const std::unordered_map<std::string, std::string>& germanTable();      // I18nDe.cpp
const std::unordered_map<std::string, std::string>& japaneseTable();    // I18nJa.cpp
const std::unordered_map<std::string, std::string>& russianTable();     // I18nRu.cpp
const std::unordered_map<std::string, std::string>& koreanTable();      // I18nKo.cpp

namespace {
std::mutex gMutex;
bool gLoaded = false;
Language gPreference = Language::Auto;
Language gActive = Language::English;

juce::File settingsFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Schematic Amp Sim").getChildFile("language.txt");
}

// What the settings file says for a language.
juce::String languageKey(Language lang) {
    switch (lang) {
        case Language::English: return "english";
        case Language::Spanish: return "spanish";
        case Language::Portuguese: return "portuguese";
        case Language::Italian: return "italian";
        case Language::Chinese: return "chinese";
        case Language::French: return "french";
        case Language::German: return "german";
        case Language::Japanese: return "japanese";
        case Language::Russian: return "russian";
        case Language::Korean: return "korean";
        default: return "auto";
    }
}

Language resolve(Language pref) {
    if (pref == Language::Auto) return fromSystemCode(juce::SystemStats::getUserLanguage());
    return pref;
}

void ensureLoaded() {
    if (gLoaded) return;
    gLoaded = true;
    const auto text = settingsFile().loadFileAsString().trim().toLowerCase();
    gPreference = Language::Auto;
    for (auto lang : {Language::English, Language::Spanish, Language::Portuguese, Language::Italian, Language::Chinese, Language::French, Language::German, Language::Japanese, Language::Russian, Language::Korean})
        if (text == languageKey(lang)) gPreference = lang;
    gActive = resolve(gPreference);
}
}  // namespace

Language fromSystemCode(const juce::String& code) {
    const auto c = code.trim().toLowerCase();
    if (c.startsWith("es")) return Language::Spanish;
    if (c.startsWith("pt")) return Language::Portuguese;
    if (c.startsWith("it")) return Language::Italian;
    if (c.startsWith("zh")) return Language::Chinese;
    if (c.startsWith("fr")) return Language::French;
    if (c.startsWith("de")) return Language::German;
    if (c.startsWith("ja")) return Language::Japanese;
    if (c.startsWith("ru")) return Language::Russian;
    if (c.startsWith("ko")) return Language::Korean;
    return Language::English;
}

Language preference() {
    std::lock_guard<std::mutex> lock(gMutex);
    ensureLoaded();
    return gPreference;
}

void setPreference(Language lang) {
    std::lock_guard<std::mutex> lock(gMutex);
    ensureLoaded();
    gPreference = lang;
    gActive = resolve(lang);
    const auto file = settingsFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText(languageKey(lang));
}

void useForSession(Language lang) {
    std::lock_guard<std::mutex> lock(gMutex);
    ensureLoaded();
    gActive = lang == Language::Auto ? fromSystemCode(juce::SystemStats::getUserLanguage()) : lang;
}

Language active() {
    std::lock_guard<std::mutex> lock(gMutex);
    ensureLoaded();
    return gActive;
}

juce::String languageName(Language lang) {
    switch (lang) {
        case Language::English: return "English";
        case Language::Spanish: return juce::String::fromUTF8("Espa\xc3\xb1ol");
        case Language::Portuguese: return juce::String::fromUTF8("Portugu\xc3\xaas");
        case Language::Italian: return "Italiano";
        case Language::Chinese: return juce::String::fromUTF8("\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87");  // 简体中文
        case Language::French: return juce::String::fromUTF8("Fran\xc3\xa7" "ais");
        case Language::German: return "Deutsch";
        case Language::Japanese: return juce::String::fromUTF8("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");  // 日本語
        case Language::Russian: return juce::String::fromUTF8("\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9");  // Русский
        case Language::Korean: return juce::String::fromUTF8("\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4");  // 한국어
        default: return "Auto";
    }
}

juce::String lookup(const juce::String& english) {
    if (english.isEmpty()) return english;
    Language lang;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        ensureLoaded();
        lang = gActive;
    }
    if (lang == Language::English || lang == Language::Auto) return english;
    const auto& table = lang == Language::Spanish      ? spanishTable()
                        : lang == Language::Portuguese ? portugueseTable()
                        : lang == Language::Italian    ? italianTable()
                        : lang == Language::Chinese    ? chineseTable()
                        : lang == Language::French     ? frenchTable()
                        : lang == Language::German     ? germanTable()
                        : lang == Language::Japanese   ? japaneseTable()
                        : lang == Language::Russian    ? russianTable()
                                                       : koreanTable();
    const auto it = table.find(english.toStdString());
    return it == table.end() ? english : juce::String::fromUTF8(it->second.c_str());
}

std::size_t translationCount(Language lang) {
    switch (lang) {
        case Language::Spanish: return spanishTable().size();
        case Language::Portuguese: return portugueseTable().size();
        case Language::Italian: return italianTable().size();
        case Language::Chinese: return chineseTable().size();
        case Language::French: return frenchTable().size();
        case Language::German: return germanTable().size();
        case Language::Japanese: return japaneseTable().size();
        case Language::Russian: return russianTable().size();
        case Language::Korean: return koreanTable().size();
        default: return 0;
    }
}

juce::String namMessage(const juce::String& english) {
    if (english.startsWith("Trained at ")) {  // "Trained at 44100 Hz, played at 48000 Hz: it will sound off-pitch."
        const auto numbers = english.fromFirstOccurrenceOf("Trained at ", false, false);
        const auto a = numbers.upToFirstOccurrenceOf(" Hz", false, false);
        const auto b = numbers.fromFirstOccurrenceOf("played at ", false, false).upToFirstOccurrenceOf(" Hz", false, false);
        return lookup(juce::String("Trained at {a} Hz, played at {b} Hz: it will sound off-pitch.")).replace("{a}", a).replace("{b}", b);
    }
    return lookup(english);
}

}  // namespace i18n
