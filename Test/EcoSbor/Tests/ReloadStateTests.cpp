#include "Launch/EcoScene.h"
#include <cstdio>
#include <stdexcept>

int main()
{
    try {
        Beast::Window window; window.width = window.contentWidth = 430; window.height = window.contentHeight = 720;
        Eco::Scene original;
        for (int tab = 0; tab < 4; ++tab) {
            original.SelectTab(tab); original.ui.BuildFrame(window, 0);
            original.pages[tab]->SetOffset(45.25f * (tab + 1));
        }
        original.darkMode = true; original.notifications = false; original.expanded[2] = true;
        original.SelectTab(0); original.ui.BuildFrame(window, 0); original.statistics->SetOffset(35.5f);
        original.SelectTab(3); original.ui.BuildFrame(window, 0);
        original.profileNameInput->SetText("Анна \"Эко\" \\ Герой");
        original.SelectTab(0); original.SelectTab(3); original.ui.BuildFrame(window, 0);
        if (original.profileNameInput->GetText() != "Анна \"Эко\" \\ Герой") throw std::runtime_error("Tab switch lost profile name.");
        const auto saved = original.SaveReloadState();
        Eco::Scene candidate;
        if (!candidate.RestoreReloadState(saved, window)) throw std::runtime_error("State restore rejected valid version.");
        candidate.ui.BuildFrame(window, 0);
        if (candidate.profileNameInput->GetText() != original.profileNameInput->GetText()) throw std::runtime_error("Reload lost profile name.");
        if (candidate.SaveReloadState() != saved) throw std::runtime_error("State round trip changed tab/preferences/offsets.");
        if (candidate.RestoreReloadState("EcoSbor/999 0", window)) throw std::runtime_error("Unsupported state version accepted.");
        if (original.SaveReloadState() != saved) throw std::runtime_error("Candidate validation mutated the active scene.");
        std::puts("EcoSbor hot reload state: tabs, accordions, preferences, horizontal/vertical offsets, version rejection and last-good isolation passed.");
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
