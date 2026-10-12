#include "UI/UI.h"
#include "UI/Private/Font.h"
#include "Core/Application.h"
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include <cstdio>
#include <stdexcept>
#include <deque>
using namespace Beast;
using namespace Beast::UI;
namespace {
    void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    template<class F> void Reject(F f) {
        bool rejected = false; try { f(); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "Expected invalid input to be rejected");
    }
    void Model() {
        TextEditor text("А😀Z");
        Check(TextEditor::Boundaries(text.GetValue().text) == std::vector<std::size_t>({0, 2, 6, 7}), "UTF-8 boundaries");
        text.Move(true); Check(text.GetValue().selection.caret == 6, "Move before ASCII");
        Check(text.Delete(true) && text.GetValue().text == "АZ", "Delete whole supplementary scalar");
        Check(text.Undo() && text.GetValue().text == "А😀Z" && text.GetValue().selection.caret == 6, "Undo caret");
        Check(text.Redo() && text.GetValue().text == "АZ", "Redo");
        text.Select({3, 0}); Check(text.ReplaceSelection("Б") && text.GetValue().text == "Б", "Reversed selection");
        text.Undo(); Check(text.GetValue().selection == TextSelection{3, 0}, "Selection undo");
        const auto before = text.GetValue();
        Check(!text.ReplaceSelection("\xc0\x80") && text.GetValue() == before, "Reject overlong atomically");
        Check(!text.ReplaceSelection("\xed\xa0\x80"), "Reject surrogate");
        Check(!text.ReplaceSelection("\xf4\x90\x80\x80"), "Reject out-of-range scalar");
        Check(!text.ReplaceSelection("\xe2\x82"), "Reject truncated sequence");
        Check(!text.ReplaceSelection("a\nb"), "Single-line controls");
        Reject([&] { text.Select({1, 1}); });
        Reject([&] { text.Select({999, 999}); });
        TextEditor limited("А", 4); Check(limited.ReplaceSelection("Б"), "Exact byte limit");
        Check(!limited.ReplaceSelection("В") && limited.GetValue().text == "АБ", "Byte limit atomicity");
        limited.SelectAll(); Check(limited.ReplaceSelection("😀"), "Replacement accounts for removed bytes");
        text.SetText(""); Check(!text.Undo() && !text.Delete(true) && !text.Delete(false), "Empty and history reset");
        for (int i = 0; i < 100; ++i) text.ReplaceSelection("a");
        int count = 0; while (text.Undo()) ++count; Check(count == 64, "Bounded history");
        text.ReplaceSelection("b"); Check(!text.Redo(), "New edit clears redo");
        Check(TextEditor::Encode(U'😀') == "😀", "Scalar encoding");
        Reject([] { TextEditor::Encode(static_cast<char32_t>(0xd800)); });
        const auto carets = Beast::UI::Detail::TextCaretPositions("AVATAR Привет", 16);
        Check(std::abs(carets.back() - Beast::UI::Detail::MeasureText("AVATAR Привет", 16).width) < .001f, "Caret and renderer kerning agree");
    }
    void EditingActions() {
        // Pointer hit testing must never place a caret inside a grapheme,
        // even though the current renderer measures individual scalars.
        {
            Window w; w.width = w.contentWidth = 500; w.height = w.contentHeight = 200;
            auto f = std::make_shared<TextField>("e\u0301👍🏽Z");
            Context context; context.SetRoot(f); context.BuildFrame(w, 0);
            const auto clusters = TextEditor::GraphemeBoundaries(f->GetText());
            Event event; event.type = EventType::MouseDown; event.x = 13; event.y = 20;
            context.HandleEvent(event, w);
            for (int x = 13; x < 120; ++x) {
                event.type = EventType::MouseMove; event.x = static_cast<float>(x); context.HandleEvent(event, w);
                Check(std::binary_search(clusters.begin(), clusters.end(), f->GetSelection().caret), "Pointer caret is on grapheme boundary");
            }
            event.type = EventType::MouseUp; context.HandleEvent(event, w);
        }
        struct FakeClipboard : Clipboard {
            std::optional<std::u16string> value; bool writable = true; int reads = 0;
            std::optional<std::u16string> ReadClipboard() override { ++reads; return value; }
            bool WriteClipboard(const std::u16string& text) override { if (!writable) return false; value = text; return true; }
        };
        auto clipboard = std::make_shared<FakeClipboard>();
        auto field = std::make_shared<TextField>("Привет мир", TextFieldOptions{.maxBytes = 32});
        field->SetSelection({12, 0});
        Check(field->Edit(TextEditAction::Copy, clipboard.get()) && clipboard->value == u"Привет", "Copy reversed UTF8 selection");
        clipboard->writable = false;
        Check(!field->Edit(TextEditAction::Cut, clipboard.get()) && field->GetText() == "Привет мир", "Failed clipboard write cannot cut");
        clipboard->writable = true;
        Check(field->Edit(TextEditAction::Cut, clipboard.get()) && field->GetText() == " мир", "Cut selected text");
        Check(field->Edit(TextEditAction::Paste, clipboard.get()) && field->GetText() == "Привет мир", "Paste UTF16 text");
        for (const auto& invalid : {std::u16string(u"a\nb"), std::u16string(1, 0xd800), std::u16string(40, u'x')}) {
            clipboard->value = invalid;
            Check(!field->Edit(TextEditAction::Paste, clipboard.get()) && field->GetText() == "Привет мир", "Invalid paste rejected atomically");
        }
        TextField readOnly("fixed", TextFieldOptions{.readOnly = true}); readOnly.Edit(TextEditAction::SelectAll);
        Check(readOnly.Edit(TextEditAction::Copy, clipboard.get()) && !readOnly.Edit(TextEditAction::Cut, clipboard.get()) && !readOnly.Edit(TextEditAction::Paste, clipboard.get()), "Readonly allows copy only");

        Window window; window.width = window.contentWidth = 500; window.height = window.contentHeight = 400; window.clipboard = clipboard;
        Context ui; ui.SetRoot(std::make_shared<Column>(std::vector<WidgetPtr>{std::make_shared<Container>(nullptr, ContainerStyle{.height = 60.f}), field}));
        ui.BuildFrame(window, 0);
        const auto key = [&](Key code, bool down = true) { Event e; e.type = down ? EventType::KeyDown : EventType::KeyUp; e.key = code; ui.HandleEvent(e, window); };
        key(Key::Tab); key(Key::Control); key(Key::A); key(Key::C); key(Key::Control, false);
        Check(clipboard->value == u"Привет мир", "Ctrl+C routes to platform clipboard");
        field->SetText("Привет мир");
        const auto bounds = field->GetBounds();
        const auto pointer = [&](EventType type, float x, float y) { Event e; e.type = type; e.x = x; e.y = y; e.pointerId = 3; ui.HandleEvent(e, window); };
        for (int i = 0; i < 2; ++i) { pointer(EventType::MouseDown, bounds.x + 18, bounds.y + 20); pointer(EventType::MouseUp, bounds.x + 18, bounds.y + 20); }
        Check(field->GetSelection() == TextSelection{0, 12}, "Double click selects Cyrillic word");
        field->SetSelection({0, 0});
        pointer(EventType::TouchDown, bounds.x + 18, bounds.y + 20);
        for (int i = 0; i < 6; ++i) ui.BuildFrame(window, .1);
        pointer(EventType::TouchUp, bounds.x + 18, bounds.y + 20);
        Check(field->GetSelection() == TextSelection{0, 12}, "Long press selects without collapsing on release");
        const int reads = clipboard->reads; ui.BuildFrame(window, 0);
        Check(clipboard->reads == reads, "Showing menu never reads clipboard");
        pointer(EventType::TouchDown, bounds.x + 20, bounds.y - 24); pointer(EventType::TouchUp, bounds.x + 20, bounds.y - 24);
        Check(clipboard->value == u"Привет", "Touch toolbar copy");
        pointer(EventType::TouchDown, bounds.x + 12, bounds.y + bounds.height - 8);
        pointer(EventType::TouchMove, bounds.x + 35, bounds.y + bounds.height - 8);
        pointer(EventType::TouchUp, bounds.x + 35, bounds.y + bounds.height - 8);
        Check(field->GetSelection().anchor > 0 && field->GetSelection().caret == 12, "Touch start handle changes selection anchor");
    }
    void ImeTests() {
        Check(TextEditor::ToUtf16("А😀Z") == u"А😀Z", "UTF16 encoding");
        Check(TextEditor::FromUtf16(u"А😀Z") == "А😀Z", "UTF16 decoding");
        Check(TextEditor::Utf16ToByte("А😀Z", 3) == 6 && TextEditor::ByteToUtf16("А😀Z", 6) == 3, "IME offset units");
        Reject([] { TextEditor::Utf16ToByte("А😀Z", 2); });
        Reject([] { TextEditor::FromUtf16(std::u16string(1, 0xd800)); });
        TextEditor model("", 16);
        Check(model.ApplyIme({"п", {2, 2}, TextSelection{0, 2}}), "Begin composing");
        const auto before = model.GetValue();
        Check(!model.ReplaceSelection("\n") && model.GetValue() == before, "Invalid edit preserves composition");
        Check(!model.ApplyIme({"п", {1, 1}, {}}) && model.GetValue() == before, "Invalid IME selection atomicity");
        Check(model.ApplyIme({"пр", {4, 4}, TextSelection{0, 4}}), "Replace composing text");
        Check(model.ApplyIme({"Привет", {12, 12}, {}}), "Commit autocorrection");
        Check(model.Undo() && model.GetValue().text.empty() && !model.Undo(), "One composition is one undo");
        Check(model.Redo() && model.GetValue().text == "Привет" && !model.GetValue().composing, "Redo committed composition");

        struct Bridge : TextInput {
            NativeTextState state; std::deque<NativeTextState> events; int updates = 0, closes = 0;
            void Update(const NativeTextState& value, int) override { state = value; ++updates; }
            void Close(std::uint64_t) override { ++closes; }
            std::optional<NativeTextState> Poll() override { if (events.empty()) return {}; auto value = events.front(); events.pop_front(); return value; }
        };
        auto bridge = std::make_shared<Bridge>();
        Window window; window.width = window.contentWidth = 500; window.height = window.contentHeight = 600; window.textInput = bridge;
        auto field = std::make_shared<TextField>(""); auto other = std::make_shared<TextField>("other");
        auto spacer = std::make_shared<Container>(nullptr, ContainerStyle{.height = 300.f});
        auto scroll = std::make_shared<ScrollView>(std::make_shared<Column>(std::vector<WidgetPtr>{spacer, field, other}));
        Context ui; ui.SetRoot(scroll); ui.BuildFrame(window, 0);
        const auto tab = [&] { Event e; e.type = EventType::KeyDown; e.key = Key::Tab; ui.HandleEvent(e, window); ui.BuildFrame(window, 0); };
        tab(); Check(field->IsFocused() && bridge->updates == 1, "Focus opens IME");
        auto input = bridge->state; input.text = u"пр"; input.anchor = input.caret = input.composingEnd = 2; input.composingStart = 0;
        bridge->events.push_back(input); ui.BuildFrame(window, 0);
        Check(field->GetText() == "пр" && bridge->updates == 1, "IME preedit does not restart connection");
        input.text = u"Привет"; input.anchor = input.caret = 6; input.composingStart = input.composingEnd = -1;
        bridge->events.push_back(input); ui.BuildFrame(window, 0); Check(field->GetText() == "Привет", "IME commit delivered");
        window.contentHeight = 220; window.imeBottom = 380; ui.BuildFrame(window, 0);
        Check(scroll->GetOffset() > 0 && field->GetBounds().y + field->GetBounds().height <= 220, "Keyboard reveals field in scroll view");
        field->SetText("native"); bridge->events.push_back(input); ui.BuildFrame(window, 0);
        Check(field->GetText() == "native", "Native edit wins queued old IME state");
        bridge->events.push_back(input); ui.BuildFrame(window, 0); Check(field->GetText() == "native", "Stale revision ignored");
        window.contentHeight = 600; window.imeBottom = 0; ui.BuildFrame(window, 0);
        tab(); bridge->events.push_back(input); ui.BuildFrame(window, 0);
        Check(other->GetText() == "other" && bridge->state.session != input.session && bridge->closes == 0, "Session transfer without keyboard flicker; stale events ignored");
        int submitted = 0; other->SetOnSubmitted([&](const std::string&) { ++submitted; });
        input = bridge->state; input.action = 1; bridge->events.push_back(input); ui.BuildFrame(window, 0);
        Check(submitted == 1 && !other->IsFocused(), "IME Done submits and dismisses");
        tab(); Check(ui.DismissTextInput() && !field->IsFocused(), "Back dismisses editor");
        tab(); Event lost; lost.type = EventType::FocusLost; ui.HandleEvent(lost, window);
        Check(!field->IsFocused(), "Lifecycle closes IME focus");
    }
    void WidgetTests() {
        Window window; window.width = window.contentWidth = 500; window.height = window.contentHeight = 400;
        auto first = std::make_shared<TextField>("Привет");
        auto second = std::make_shared<TextField>("fixed", TextFieldOptions{.readOnly = true});
        int clicks = 0, changes = 0, submissions = 0;
        auto button = std::make_shared<Button>("Button", [&] { ++clicks; });
        Context ui; ui.SetRoot(std::make_shared<Column>(std::vector<WidgetPtr>{first, button, second}));
        ui.BuildFrame(window, 0);
        const auto key = [&](Key code, bool down = true) { Event e; e.type = down ? EventType::KeyDown : EventType::KeyUp; e.key = code; return ui.HandleEvent(e, window); };
        const auto type = [&](char32_t cp) { Event e; e.type = EventType::TextInput; e.character = cp; return ui.HandleEvent(e, window); };
        first->SetOnChanged([&](const std::string&) { ++changes; });
        first->SetOnSubmitted([&](const std::string&) { ++submissions; });
        Check(key(Key::Tab) && first->IsFocused(), "Tab focuses field");
        Check(type(U'!') && first->GetText() == "Привет!" && changes == 1, "Committed input");
        key(Key::Control); key(Key::A); key(Key::Control, false);
        type(U'Б'); Check(first->GetText() == "Б", "Select all and replace");
        key(Key::Control); key(Key::Z); key(Key::Control, false);
        Check(first->GetText() == "Привет!", "Keyboard undo");
        key(Key::Enter); type(U'\r'); Check(submissions == 1 && first->GetText() == "Привет!", "Submit without CR insertion");
        key(Key::Tab); Check(button->IsFocused() && !first->IsFocused(), "Mixed focus order");
        key(Key::Space); key(Key::Space, false); Check(clicks == 1, "Button activation preserved");
        key(Key::Tab); Check(second->IsFocused(), "Readonly is focusable");
        type(U'X'); key(Key::Backspace); Check(second->GetText() == "fixed", "Readonly rejects editing");
        key(Key::Shift); key(Key::Tab); key(Key::Shift, false); Check(button->IsFocused(), "Reverse focus order");
        key(Key::Tab); Check(key(Key::Escape) && !second->IsFocused(), "Escape dismisses editor focus");
        const auto r = first->GetBounds();
        Event e; e.type = EventType::MouseDown; e.x = r.x + 13; e.y = r.y + 20;
        ui.HandleEvent(e, window); e.type = EventType::MouseMove; e.x = r.x + r.width - 12; ui.HandleEvent(e, window);
        e.type = EventType::MouseUp; ui.HandleEvent(e, window);
        Check(!first->GetSelection().Empty(), "Mouse drag selection");
        for (int i = 0; i < 20; ++i) ui.BuildFrame(window, .1); // Settle previous button focus/ripple animations.
        const auto revision = ui.GetFrameRevision();
        ui.BuildFrame(window, .1); Check(ui.GetFrameRevision() == revision, "Selected text does not animate caret");
        first->SetSelection({0, 0}); ui.BuildFrame(window, 0); const auto beforeBlink = ui.GetFrameRevision();
        ui.BuildFrame(window, .1); Check(ui.GetFrameRevision() == beforeBlink, "Caret avoids repaint every frame");
        for (int i = 0; i < 5; ++i) ui.BuildFrame(window, .1);
        Check(ui.GetFrameRevision() > beforeBlink, "Caret blink invalidates paint");
        e.type = EventType::FocusLost; ui.HandleEvent(e, window); Check(!first->IsFocused(), "Native focus loss");
        key(Key::Tab);
        first->SetOnChanged([&](const std::string&) { ui.SetRoot(std::make_shared<Text>("replaced")); });
        type(U'X'); Check(!first->IsFocused(), "Callback may detach focused field safely");
    }
#if defined(_WIN32)
    class NativeTest final : public Beast::Application {
        RHIBackend backend_;
        Context ui_;
        std::shared_ptr<TextField> field_ = std::make_shared<TextField>();
        std::unique_ptr<RHI> renderer_;
        int frames_ = 0;
    public:
        explicit NativeTest(RHIBackend backend) : backend_(backend) { ui_.SetRoot(std::make_shared<Center>(field_)); }
        WindowDefinition Configure() const override {
            WindowDefinition w; w.title = "Beast TextField native verification"; w.gpuRendering = true; return w;
        }
        void OnEvent(const Event& event) override {
            ui_.HandleEvent(event, GetWindow());
            if (event.type == EventType::SurfaceCreated) renderer_ = RHI::Create(GetWindow(), {backend_, true, true});
            if (event.type == EventType::SurfaceDestroyed) renderer_.reset();
        }
        void OnUpdate(double) override {
            if (!renderer_) return;
            if (frames_ == 0) {
                const auto hwnd = static_cast<HWND>(GetWindow().nativeHandle);
                SendMessageW(hwnd, WM_KEYDOWN, VK_TAB, 0);
                SendMessageW(hwnd, WM_KEYUP, VK_TAB, 0);
                SendMessageW(hwnd, WM_CHAR, L'Ж', 0);
                SendMessageW(hwnd, WM_CHAR, 0xd83d, 0);
                SendMessageW(hwnd, WM_CHAR, 0xde00, 0);
                Check(field_->GetText() == "Ж😀", "Native WM_CHAR surrogate pair routing");
                SendMessageW(hwnd, WM_KEYDOWN, VK_BACK, 0);
                SendMessageW(hwnd, WM_CHAR, 8, 0);
                SendMessageW(hwnd, WM_KEYUP, VK_BACK, 0);
                Check(field_->GetText() == "Ж", "Native backspace deletes once");
                field_->SetText("Привет, Beast TextField!");
                field_->SetSelection({0, 12});
            }
            Check(ui_.Render(*renderer_, GetWindow(), .016), "TextField GPU submission");
            if (++frames_ == 10) RequestExit();
        }
    };
#endif
}
int main() {
    try {
        Model(); EditingActions(); ImeTests(); WidgetTests();
#if defined(_WIN32)
        for (auto backend : {RHIBackend::OpenGL, RHIBackend::Vulkan}) {
            NativeTest test(backend); Check(Run(test) == 0, "Native test run");
        }
#endif
        std::puts("Text editor / field tests and Windows native input + OpenGL/Vulkan frames passed."); return 0;
    }
    catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
