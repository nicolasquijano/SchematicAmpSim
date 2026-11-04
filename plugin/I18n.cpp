#include "I18n.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace i18n {

const std::unordered_map<std::string, std::string>& spanishTable();  // I18nEs.cpp

namespace {
std::mutex gMutex;
bool gLoaded = false;
Language gPreference = Language::Auto;
Language gActive = Language::English;

juce::File settingsFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Schematic Amp Sim").getChildFile("language.txt");
}

Language resolve(Language pref) {
    if (pref == Language::Auto) return fromSystemCode(juce::SystemStats::getUserLanguage());
    return pref;
}

void ensureLoaded() {
    if (gLoaded) return;
    gLoaded = true;
    const auto text = settingsFile().loadFileAsString().trim().toLowerCase();
    gPreference = text == "english" ? Language::English : text == "spanish" ? Language::Spanish : Language::Auto;
    gActive = resolve(gPreference);
}
}  // namespace

Language fromSystemCode(const juce::String& code) { return code.trim().toLowerCase().startsWith("es") ? Language::Spanish : Language::English; }

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
    file.replaceWithText(lang == Language::English ? "english" : lang == Language::Spanish ? "spanish" : "auto");
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
        default: return "Auto";
    }
}

juce::String lookup(const juce::String& english) {
    if (english.isEmpty()) return english;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        ensureLoaded();
        if (gActive != Language::Spanish) return english;
    }
    const auto& table = spanishTable();
    const auto it = table.find(english.toStdString());
    return it == table.end() ? english : juce::String::fromUTF8(it->second.c_str());
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
