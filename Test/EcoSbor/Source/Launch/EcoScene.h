#pragma once
#include "Launch/EcoData.h"
#include "UI/Canvas.h"

namespace Eco
{
    class Scene : public Beast::UI::Scene
    {
        std::array<std::shared_ptr<Column>, 4> contents_;
        std::string profileName_ = "Эко-Герой";
        WidgetPtr Home();
        void Guide();
        WidgetPtr Map();
        void Profile();
        void Compose();
    public:
        int tab = 0;
        bool darkMode = false, notifications = true;
        std::array<bool, 6> expanded{};
        std::array<std::shared_ptr<ScrollView>, 4> pages;
        std::shared_ptr<ScrollView> statistics;
        std::array<std::shared_ptr<Pressable>, 4> navigation;
        std::array<std::shared_ptr<Pressable>, 6> guideHeaders;
        std::shared_ptr<Pressable> darkSwitch, notificationSwitch;
        std::shared_ptr<TextField> profileNameInput;
        Scene();
        void SelectTab(int index);
        std::string SaveReloadState() const override;
        bool RestoreReloadState(const std::string&, const Beast::Window&) override;
        bool OnBack() override
        { if (tab == 0) return false; SelectTab(0); return true; }
    };
}
