#include "Beast.h"

class TextFieldDemoScene final : public Beast::UI::Scene
{
    std::shared_ptr<Beast::UI::TextField> name_;
public:
    TextFieldDemoScene()
    {
        using namespace Beast::UI;
        using namespace Beast::UI::Build;
        name_ = Make<TextField>("Эко-Герой", TextFieldOptions{.placeholder = "Введите имя", .maxBytes = 256});
        auto note = Make<TextField>("", TextFieldOptions{.placeholder = "Ваше сообщение", .maxBytes = 1024, .multiline = true, .minLines = 3, .maxLines = 5});
        auto password = Make<TextField>("", TextFieldOptions{.placeholder = "Пароль", .maxBytes = 256, .password = true});
        auto required = Make<TextField>("", TextFieldOptions{.placeholder = "Обязательное поле", .maxBytes = 256,
            .validator = [](const std::string& value) -> std::optional<std::string> { return value.empty() ? std::optional<std::string>("Введите значение") : std::nullopt; }});
        auto rtl = Make<TextField>("Beast — مرحبا — שלום", TextFieldOptions{.maxBytes = 256});
        rtl->SetAccessibilityLabel("Смешанный текст: русский, арабский и иврит");
        auto status = Make<Text>("Enter — подтвердить", 16);
        name_->SetOnSubmitted([status](const std::string& text) { status->SetText("Привет, " + text + "!"); });
        auto readOnly = Make<TextField>("Только чтение", TextFieldOptions{.readOnly = true});
        ui.SetRoot(Make<ScrollView>(Inset(VBox({Make<Text>("Beast TextField", 28),
            Make<Text>("Имя", 14), name_, Make<Text>("RTL / LTR", 14), rtl, Make<Text>("Сообщение", 14), note,
            Make<Text>("Пароль", 14), password, required, Make<Button>("Проверить", [required] { required->Validate(); }),
            Make<Button>("Сбросить имя", [this] { name_->SetText("Эко-Герой"); }),
            readOnly, status, Make<Text>("Tab / Shift+Tab · Ctrl+A · Ctrl+Z / Y", 12)}, 12), 20, 24)));
    }
    std::string SaveReloadState() const override { return name_->GetText(); }
    bool RestoreReloadState(const std::string& state, const Beast::Window&) override
    { name_->SetText(state); return true; }
};

#if defined(BEAST_RHI_VULKAN)
BEAST_UI_APPLICATION(TextFieldDemoScene, {.title = "TextFieldDemo — Beast", .backend = Beast::RHIBackend::Vulkan})
#else
BEAST_UI_APPLICATION(TextFieldDemoScene, {.title = "TextFieldDemo — Beastsss"})
#endif
