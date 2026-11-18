#pragma once
// What the interface remembers between sessions and between instances of the plug-in (not part of a project's state): the favourite amps, the
// amps used last, and the size of the window. A small file in the user's application-data folder; every call reads or writes it directly,
// they happen on clicks and on a window resize, never on the audio thread.

#include <juce_data_structures/juce_data_structures.h>

class UiSettings {
public:
    static juce::StringArray favourites() { return list("favourites"); }
    static bool isFavourite(const juce::String& ampId) { return favourites().contains(ampId); }
    static void toggleFavourite(const juce::String& ampId) {
        auto f = favourites();
        if (f.contains(ampId)) f.removeString(ampId);
        else f.add(ampId);
        store("favourites", f);
    }

    // The window size, 0 x 0 when none was saved yet.
    static juce::Point<int> windowSize() {
        auto file = open();
        return {file->getIntValue("windowW", 0), file->getIntValue("windowH", 0)};
    }
    static void setWindowSize(int w, int h) {
        auto file = open();
        if (file->getIntValue("windowW", 0) == w && file->getIntValue("windowH", 0) == h) return;
        file->setValue("windowW", w);
        file->setValue("windowH", h);
        file->saveIfNeeded();
    }

private:
    static std::unique_ptr<juce::PropertiesFile> open() {
        juce::PropertiesFile::Options o;
        o.applicationName = "Schematic Amp Sim";
        o.filenameSuffix = ".settings";
        o.folderName = "Schematic Amp Sim";
        o.osxLibrarySubFolder = "Application Support";
        o.commonToAllUsers = false;
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        // SAS_UI_SETTINGS_DIR: another folder for the file (the tests use it so they do not touch the user's own).
        if (const auto dir = juce::SystemStats::getEnvironmentVariable("SAS_UI_SETTINGS_DIR", {}); dir.isNotEmpty())
            return std::make_unique<juce::PropertiesFile>(juce::File(dir).getChildFile("ui.settings"), o);
        return std::make_unique<juce::PropertiesFile>(o);
    }
    static juce::StringArray list(const char* key) {
        juce::StringArray out;
        out.addTokens(open()->getValue(key, ""), ",", "");
        out.removeEmptyStrings();
        return out;
    }
    static void store(const char* key, const juce::StringArray& values) {
        auto file = open();
        file->setValue(key, values.joinIntoString(","));
        file->saveIfNeeded();
    }
};
