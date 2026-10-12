#pragma once
#include "Beast.h"

// The same retained widget tree is used by Windows/Android and both RHI backends.
class ButtonScene
{
public:
    Beast::UI::Context ui;
    std::shared_ptr<Beast::UI::Text> counter;
    std::shared_ptr<Beast::UI::Button> primary, toggle, disabled;
    std::shared_ptr<Beast::UI::Expanded> leftPane, rightPane;
    std::shared_ptr<Beast::UI::Text> leftLabel, rightLabel;
    int clicks = 0;
    ButtonScene()
    {
        using namespace Beast::UI;
        Theme theme;
        theme.background = {0.08f, 0.07f, 0.09f, 1};
        theme.primary = {0.82f, 0.737f, 0.98f, 1};
        theme.onPrimary = {0.22f, 0.16f, 0.36f, 1};
        theme.onSurface = {0.90f, 0.88f, 0.92f, 1};
        ui.SetTheme(theme);
        ContainerStyle card;
        card.height = 64.0f; card.padding = EdgeInsets::All(12);
        card.background = Beast::RHIColor{0.21f, 0.16f, 0.30f, 1};
        card.cornerRadius = 14; card.alignment = Alignment::Center;
        leftLabel = std::make_shared<Text>("Flex 1", 14.0f);
        rightLabel = std::make_shared<Text>("Flex 2", 14.0f);
        leftPane = std::make_shared<Expanded>(std::make_shared<Container>(leftLabel, card), 1u);
        card.background = Beast::RHIColor{0.16f, 0.20f, 0.28f, 1};
        rightPane = std::make_shared<Expanded>(std::make_shared<Container>(rightLabel, card), 2u);
        counter = std::make_shared<Text>("Нажатий: 0", 18.0f);
        primary = std::make_shared<Button>("Нажми меня", [this] {
            counter->SetText("Нажатий: " + std::to_string(++clicks));
            const unsigned left = clicks % 2 ? 2u : 1u, right = 3u - left;
            leftPane->SetFlex(left); rightPane->SetFlex(right);
            leftLabel->SetText("Flex " + std::to_string(left)); rightLabel->SetText("Flex " + std::to_string(right));
            Beast::Log(("ButtonDemo: click " + std::to_string(clicks)).c_str());
        });
        toggle = std::make_shared<Button>("Отключить", [this] {
            primary->SetEnabled(!primary->IsEnabled());
            toggle->SetText(primary->IsEnabled() ? "Отключить" : "Включить");
        });
        disabled = std::make_shared<Button>("Недоступно"); // Null callback => disabled, skipped by Tab/hit test.
        auto column = std::make_shared<Column>(std::vector<WidgetPtr>{
            std::make_shared<Text>("Beast UI", 32.0f),
            std::make_shared<Text>("Row / Column / Expanded", 14.0f),
            std::make_shared<Row>(std::vector<WidgetPtr>{leftPane, rightPane}, 12.0f),
            std::make_shared<Center>(counter),
            std::make_shared<Row>(std::vector<WidgetPtr>{std::make_shared<Expanded>(primary), std::make_shared<Expanded>(toggle)}, 12.0f),
            disabled, std::make_shared<Text>("Мышь / касание / Tab + Space", 12.0f)
        }, FlexOptions{12, MainAxisSize::Min, MainAxisAlignment::Start, CrossAxisAlignment::Stretch});
        ContainerStyle panel;
        panel.constraints.maxWidth = 640; panel.padding = EdgeInsets::All(20);
        panel.background = Beast::RHIColor{0.12f, 0.105f, 0.15f, 1}; panel.cornerRadius = 24;
        ui.SetRoot(std::make_shared<Padding>(24.0f, std::make_shared<Center>(std::make_shared<Container>(column, panel))));
    }
};
