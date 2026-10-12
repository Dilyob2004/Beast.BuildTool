#include "Launch/EcoScene.h"
#include <sstream>
#include <iomanip>

namespace Eco
{
    using namespace Beast::UI::Build;
    namespace
    {
        using Ptr = WidgetPtr;
        auto Label(std::string text, float size = 14, RHIColor color = Ink, bool bold = false, TextAlign align = TextAlign::Start, unsigned lines = 0)
        {
            ParagraphStyle style; style.fontSize = size; style.color = color; style.face = bold ? FontFace::Bold : FontFace::Regular;
            style.align = align; style.maxLines = lines; style.ellipsis = lines != 0; style.lineHeight = size > 16 ? 1.2f : 1.4f;
            return Make<Paragraph>(std::move(text), style);
        }
        auto Symbol(char32_t cp, float size = 24, RHIColor color = Green) { return Make<Icon>(cp, size, color); }
        ContainerStyle CardStyle(RHIColor color, float tint = .08f, float padding = 16)
        {
            ContainerStyle s; s.background = Tint(color, tint); s.cornerRadius = 16; s.padding = EdgeInsets::All(padding);
            s.borderColor = Tint(color, .25f); s.borderWidth = 1; return s;
        }
        ContainerStyle GradientStyle(float radius, float padding = 0)
        {
            ContainerStyle s; s.gradient = ContainerStyle::Gradient{Green, Color(0x00897b)}; s.cornerRadius = radius;
            s.padding = EdgeInsets::All(padding); s.shadowColor = Color(0x2e7d32, .3f); s.shadowBlur = 16; s.shadowOffset = {0, 6}; return s;
        }
        auto IconBox(char32_t cp, RHIColor color = Green, float size = 24, float pad = 10, float radius = 12)
        {
            ContainerStyle s; s.background = Tint(color, .15f); s.padding = EdgeInsets::All(pad); s.cornerRadius = radius;
            return Box(Symbol(cp, size, color), s);
        }
        auto Section(const char* title) { return Inset(Label(title, 18, Dark, true), 20); }
        // A small eager grid for six tiles. This is not a virtualized list.
        class TileGrid final : public Widget
        {
            float ratio_;
            int Columns(float width) const { return width < 300 ? 2 : 3; }
            Size OnMeasure(const Constraints& c) const override
            {
                if (!std::isfinite(c.maxWidth)) throw std::invalid_argument("TileGrid needs a bounded width.");
                const int count = Columns(c.maxWidth), rows = (static_cast<int>(Children().size()) + count - 1) / count;
                const float cell = std::max(0.0f, (c.maxWidth - (count - 1) * 12) / count);
                return {c.maxWidth, rows ? rows * cell / ratio_ + (rows - 1) * 12 : 0};
            }
            void OnLayout() override
            {
                const auto r = GetBounds(); const int count = Columns(r.width); const float cell = std::max(0.0f, (r.width - (count - 1) * 12) / count);
                int i = 0; for (const auto& child : Children())
                {
                    child->Measure(Constraints::Tight({cell, cell / ratio_}));
                    child->Layout({r.x + (i % count) * (cell + 12), r.y + (i / count) * (cell / ratio_ + 12), cell, cell / ratio_}); ++i;
                }
            }
        public:
            TileGrid(std::vector<Ptr> children, float ratio = 1) : ratio_(ratio) { for (auto& child : children) Adopt(std::move(child)); }
        };
        class Progress final : public Widget
        {
            Size OnMeasure(const Constraints&) const override { return {100, 10}; }
            void OnPaint(Canvas& c) const override
            {
                auto r = GetBounds(); c.RoundedRect(r, 5, Tint(Green, .22f)); r.width *= .35f; c.RoundedRect(r, 5, Green);
            }
        };
        class SwitchMark final : public Widget
        {
            bool value_;
            Size OnMeasure(const Constraints&) const override { return {44, 32}; }
            void OnPaint(Canvas& c) const override
            {
                auto r = GetBounds(); r.y += (r.height - 24) / 2; r.height = 24;
                c.RoundedRect(r, 12, value_ ? Color(0xa5d6a7) : Color(0xe0e0e0));
                c.RoundedRect({r.x + (value_ ? r.width - 22 : 2), r.y + 2, 20, 20}, 10, value_ ? Green : Color(0x9e9e9e));
            }
        public: explicit SwitchMark(bool value) : value_(value) {}
        };
    }
    Scene::Scene()
    {
        Theme theme; theme.background = White; theme.primary = Green; theme.onPrimary = White; theme.onSurface = Ink; ui.SetTheme(theme);
        for (auto& content : contents_) content = VBox({});
        contents_[0]->Add(Home()); contents_[2]->Add(Map()); Guide(); Profile();
        for (int i = 0; i < 4; ++i) {
            pages[i] = Make<ScrollView>(contents_[i]);
            pages[i]->SetRasterization(ScrollRasterization::Subpixel);
        }
        Compose();
    }
    void Scene::SelectTab(int index)
    {
        if (index < 0 || index >= 4) throw std::invalid_argument("Invalid EcoSbor tab.");
        if (tab != index) { tab = index; Compose(); Beast::Log(("EcoSbor tab=" + std::to_string(tab)).c_str()); }
    }
    std::string Scene::SaveReloadState() const
    {
        std::ostringstream state; state << std::setprecision(9) << "EcoSbor/2 " << tab << ' ' << darkMode << ' ' << notifications;
        for (bool value : expanded) state << ' ' << value;
        for (const auto& page : pages) state << ' ' << std::clamp(page->GetOffset(), 0.f, page->GetExtent());
        state << ' ' << std::clamp(statistics->GetOffset(), 0.f, statistics->GetExtent());
        state << ' ' << std::quoted(profileNameInput->GetText());
        return state.str();
    }
    bool Scene::RestoreReloadState(const std::string& serialized, const Beast::Window& window)
    {
        if (serialized.empty()) return true;
        std::istringstream state(serialized); std::string version; int selected = 0;
        std::array<float, 4> offsets{}; float horizontal = 0;
        if (!(state >> version >> selected >> darkMode >> notifications) || (version != "EcoSbor/1" && version != "EcoSbor/2") || selected < 0 || selected > 3) return false;
        for (auto& value : expanded) if (!(state >> value)) return false;
        for (auto& value : offsets) if (!(state >> value) || !std::isfinite(value) || value < 0) return false;
        if (!(state >> horizontal) || !std::isfinite(horizontal) || horizontal < 0) return false;
        std::string name = "Эко-Герой";
        if (version == "EcoSbor/2" && !(state >> std::quoted(name))) return false;
        try { TextEditor validated(name, 128); }
        catch (const std::invalid_argument&) { return false; }
        profileName_ = std::move(name);
        profileNameInput.reset();
        Guide(); Profile();
        // Layout the candidate, not the active scene, before restoring offsets.
        for (int i = 0; i < 4; ++i) { SelectTab(i); ui.BuildFrame(window, 0); pages[i]->SetOffset(offsets[i]); }
        SelectTab(0); ui.BuildFrame(window, 0); statistics->SetOffset(horizontal);
        SelectTab(selected);
        return true;
    }
    void Scene::Compose()
    {
        ui.SetRoot(nullptr); // Detach the active page before adopting it into the new scaffold.
        const char* labels[]{"Главная1", "Гид", "Карта", "Профиль"};
        const char* titles[]{"", "Гид по переработке", "Эко-карта", "Профиль"};
        const char32_t active[]{0xe318, 0xe3dd, 0xe3c8, 0xe491}, inactive[]{0xf107, 0xf1c2, 0xf1ae, 0xe497};
        std::vector<Ptr> buttons;
        for (int i = 0; i < 4; ++i)
        {
            const auto color = i == tab ? Green : Muted;
            auto content = VBox({Symbol(i == tab ? active[i] : inactive[i], 24, color), Label(labels[i], 12, color, i == tab, TextAlign::Center)}, 2, CrossAxisAlignment::Center);
            navigation[i] = Make<Pressable>(Sized(Centered(content), -1, 56), [this, i] { SelectTab(i); });
            buttons.push_back(Fill(navigation[i]));
        }
        ContainerStyle nav; nav.background = White; nav.shadowColor = Color(0x2e7d32, .10f); nav.shadowBlur = 12; nav.shadowOffset = {0, -4};
        auto header = tab ? Sized(Centered(Label(titles[tab], 20, Dark, true)), -1, 56) : nullptr;
        ui.SetRoot(Scaffold(pages[tab], Box(HBox(std::move(buttons), 0), nav), header, 680));
    }
    WidgetPtr Scene::Home()
    {
        ContainerStyle badge; badge.background = Color(0xffffff, .2f); badge.cornerRadius = 10; badge.padding = EdgeInsets::Symmetric(16, 10);
        ContainerStyle logo = badge; logo.cornerRadius = 12; logo.padding = EdgeInsets::All(10);
        auto banner = Box(VBox({HBox({Box(Symbol(0xe217, 28, White), logo), Fill(Label("ЭкоСбор", 28, White, true))}),
            Space(16), Label("Сделаем планету чище вместе!\nРаздельный сбор — первый шаг.", 16, Color(0xffffff, .9f)), Space(16),
            HBox({Box(HBox({Symbol(0xe25b, 16, White), Label("Каждый вклад важен", 14, White, true)}, 8), badge)}, 0)}, 0), GradientStyle(20, 24));
        auto tip = Box(HBox({IconBox(0xe37c), Fill(VBox({Label("Эко-совет дня", 14, Green, true),
            Label("Перед сдачей пластиковых бутылок снимите крышку — они перерабатываются отдельно!")}, 4))}, 16), CardStyle(Green, .10f, 20));
        std::vector<Ptr> stats;
        const char* values[]{"12.5 т", "8.3 т", "5.1 т", "3.7 т"};
        const char* names[]{"Пластика\nпереработано", "Бумаги\nспасено", "Стекла\nсобрано", "Металла\nсдано"};
        const char32_t symbols[]{0xf055a, 0xe1bf, 0xe391, 0xe57f};
        for (int i = 0; i < 4; ++i)
        {
            auto style = CardStyle(Green, 0, 16); style.width = 140.0f; style.borderColor = Color(0xe8f5e9); style.borderWidth = 1.5f;
            stats.push_back(Box(VBox({Symbol(symbols[i], 22, Color(0x4caf50)), Fill(Space(0)), Label(values[i], 18, Green, true), Label(names[i], 11, Muted)}, 2, CrossAxisAlignment::Start), style));
        }
        statistics = Make<ScrollView>(Inset(HBox(std::move(stats), 12), 16), Axis::Horizontal);
        statistics->SetRasterization(ScrollRasterization::Subpixel);
        std::vector<Ptr> tiles;
        for (const auto& c : Categories)
        {
            auto content = VBox({IconBox(c.icon, c.color, 24, 10, 50), Label(c.shortTitle, 13, c.color, true, TextAlign::Center, 1),
                Label(c.shortSubtitle, 10, Tint(c.color, .7f), false, TextAlign::Center, 1)}, 4, CrossAxisAlignment::Center);
            tiles.push_back(Box(Centered(content), CardStyle(c.color, .08f, 10)));
        }
        return VBox({Space(16), Inset(Cached(banner), 16), Space(24), Inset(Cached(tip), 16), Space(24), Section("Наш вклад"), Space(12),
            Sized(statistics, -1, 120), Space(24), Section("Что можно сдать"), Space(12), Inset(Make<TileGrid>(std::move(tiles), .95f), 16), Space(32)});
    }
    void Scene::Guide()
    {
        auto column = contents_[1]; column->Clear();
        auto intro = Box(HBox({Symbol(0xf8b4, 32), Fill(VBox({Label("Как правильно сортировать?", 18, Dark, true),
            Label("Нажмите на категорию, чтобы узнать подробности", 14, Muted)}, 4))}, 16), CardStyle(Green, .1f, 20));
        column->Add(Space(8)); column->Add(Inset(intro, 16)); column->Add(Space(20));
        for (std::size_t i = 0; i < Categories.size(); ++i)
        {
            const auto& c = Categories[i];
            auto title = HBox({IconBox(c.icon, c.color, 24, 8, 10), Fill(VBox({Label(c.title, 16, c.color, true),
                Label(c.subtitle, 12, Tint(c.color, .7f))}, 2)), Symbol(expanded[i] ? 0xe15f : 0xe246, 24, Muted)}, 16);
            guideHeaders[i] = Make<Pressable>(Inset(title, 20, 16), [this, i] {
                expanded[i] = !expanded[i]; Guide(); Beast::Log(("EcoSbor guide=" + std::to_string(i) + " expanded=" + std::to_string(expanded[i])).c_str());
            });
            std::vector<Ptr> body{guideHeaders[i]};
            if (expanded[i])
            {
                std::vector<Ptr> detail;
                auto section = [&](const char* heading, const std::vector<std::string>& items, RHIColor color) {
                    detail.push_back(Label(heading, 14, color, true)); detail.push_back(Space(6));
                    for (const auto& item : items) { detail.push_back(Label("• " + item)); detail.push_back(Space(4)); }
                    detail.push_back(Space(12));
                };
                section("Можно сдавать:", c.accepted, Green); section("Нельзя сдавать:", c.rejected, Color(0xc62828)); section("Как подготовить:", c.prepare, Color(0x388e3c));
                body.push_back(Inset(VBox(std::move(detail)), 20, 8));
            }
            column->Add(Inset(Cached(Box(VBox(std::move(body)), CardStyle(c.color, .06f, 0))), 32, 12));
        }
        column->Add(Space(32));
    }
    WidgetPtr Scene::Map()
    {
        auto logo = GradientStyle(80); logo.width = logo.height = 120.0f;
        std::vector<Ptr> features;
        const char32_t icons[]{0xf193, 0xf068, 0xefc8};
        const char* labels[]{"Ближайшие пункты приёма", "Фильтр по типу вторсырья", "Маршрут до пункта"};
        for (int i = 0; i < 3; ++i) features.push_back(Centered(HBox({IconBox(icons[i], Green, 20, 8, 8), Label(labels[i], 14, Ink, true)}, 12)));
        auto content = VBox({Centered(Box(Centered(Symbol(0xf1ae, 56, White)), logo)), Space(32), Label("Скоро здесь!", 22, Dark, true, TextAlign::Center), Space(12),
            Label("Мы работаем над картой пунктов\nприёма вторсырья в вашем городе", 16, Muted, false, TextAlign::Center), Space(32), VBox(std::move(features), 12), Space(40),
            Centered(Box(HBox({Symbol(0xf237, 20), Label("Уведомить о запуске", 14, Green, true)}, 8), CardStyle(Green, .10f, 16)))});
        // Deliberately informational, as in the Flutter source: no maps or push subscription yet.
        return Inset(VBox({Space(80), content, Space(64)}), 32);
    }
    void Scene::Profile()
    {
        if (profileNameInput) profileName_ = profileNameInput->GetText();
        auto column = contents_[3]; column->Clear();
        auto avatar = GradientStyle(60); avatar.width = avatar.height = 96.0f;
        column->Add(Space(16)); column->Add(Centered(Box(Centered(Symbol(0xe491, 48, White)), avatar))); column->Add(Space(16));
        auto nameLabel = Label(profileName_.empty() ? "Эко-Герой" : profileName_, 22, Dark, true, TextAlign::Center, 2);
        column->Add(nameLabel); column->Add(Space(4));
        column->Add(Label("Вместе мы делаем мир чище", 14, Muted, false, TextAlign::Center)); column->Add(Space(24));
        profileNameInput = Make<TextField>(profileName_, TextFieldOptions{.placeholder = "Например, Александр", .fontSize = 16, .maxBytes = 128});
        profileNameInput->SetOnChanged([this, nameLabel](const std::string& name) {
            profileName_ = name;
            nameLabel->SetText(name.empty() ? "Эко-Герой" : name);
        });
        profileNameInput->SetOnSubmitted([this](const std::string&) { ui.DismissTextInput(); });
        auto inputCard = CardStyle(Green, .045f, 18);
        inputCard.cornerRadius = 20;
        column->Add(Inset(Box(VBox({
            HBox({IconBox(0xe491, Green, 22, 10), Fill(VBox({Label("Как вас называть?", 16, Dark, true),
                Label("Ваш маленький вклад начинается с вас", 12, Muted)}, 3))}, 12),
            Space(14), Label("Ваше имя", 12, Green, true), Space(6), profileNameInput,
            Space(8), Label("Имя используется только в этом демо-профиле", 11, Muted)}, 0), inputCard), 16));
        column->Add(Space(20));
        auto status = CardStyle(Green, .10f, 20); status.gradient = ContainerStyle::Gradient{Color(0xe8f5e9), Color(0xf1f8e9), {0, 0}, {0, 1}};
        column->Add(Inset(Box(VBox({Centered(HBox({Symbol(0xe217, 20), Label("Эко-уровень: Новичок", 16, Green, true)}, 8)), Space(16),
            Make<Progress>(), Space(8), Label("350 / 1000 эко-баллов до уровня «Защитник»", 12, Muted, false, TextAlign::Center)}, 0), status), 16));
        column->Add(Space(24)); column->Add(Section("Достижения")); column->Add(Space(12));
        const char* titles[]{"Первый шаг", "10 сдач", "Эко-друг", "Мастер", "Наставник", "Легенда"};
        const char32_t icons[]{0xe28e, 0xe5f9, 0xe25b, 0xe22c, 0xe559, 0xe0b7}; std::vector<Ptr> badges;
        for (int i = 0; i < 6; ++i)
        {
            const auto color = i < 3 ? Green : Color(0xbdbdbd);
            std::vector<Ptr> content{Symbol(icons[i], 28, color), Label(titles[i], 11, i < 3 ? Dark : Color(0x9e9e9e), true, TextAlign::Center, 1)};
            if (i >= 3) content.push_back(Symbol(0xe3b1, 12, Color(0x9e9e9e)));
            auto style = CardStyle(i < 3 ? Green : Color(0x9e9e9e), .10f, 8);
            badges.push_back(Box(Centered(VBox(std::move(content), 6, CrossAxisAlignment::Center)), style));
        }
        column->Add(Inset(Make<TileGrid>(std::move(badges)), 16)); column->Add(Space(24)); column->Add(Section("Настройки")); column->Add(Space(8));
        auto setting = [&](char32_t icon, const char* title, const char* subtitle, bool value, std::function<void()> callback) {
            return Make<Pressable>(Inset(HBox({IconBox(icon, Green, 22, 8, 10), Fill(VBox({Label(title, 14, Ink, true), Label(subtitle, 12, Muted)}, 2)),
                Make<SwitchMark>(value)}, 16), 20, 12), std::move(callback));
        };
        darkSwitch = setting(0xef9f, "Тёмная тема", "Скоро будет доступна", darkMode, [this] { darkMode = !darkMode; Profile(); Beast::Log("EcoSbor dark preference toggled (theme is a placeholder)"); });
        notificationSwitch = setting(0xf237, "Уведомления", "Напоминания о сдаче вторсырья", notifications, [this] { notifications = !notifications; Profile(); Beast::Log("EcoSbor notification preference toggled (local only)"); });
        column->Add(darkSwitch); column->Add(notificationSwitch);
        const char32_t infoIcons[]{0xe33d, 0xf2d0, 0xe30b}; const char* infoTitles[]{"О приложении", "Политика конфиденциальности", "Помощь и поддержка"};
        for (int i = 0; i < 3; ++i)
        {
            auto label = VBox({Label(infoTitles[i], 14, Ink, true)});
            if (!i) label->Add(Label("Версия 1.0.0", 12, Muted));
            column->Add(Inset(HBox({IconBox(infoIcons[i], Green, 22, 8, 10), Fill(label), Symbol(0xe15f, 24, Muted)}, 16), 20, 12));
        }
        column->Add(Space(32));
    }
}
