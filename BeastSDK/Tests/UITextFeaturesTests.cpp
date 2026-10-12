#include "UI/UI.h"
#include "UI/Private/Font.h"
#include "UI/Private/TextLayout.h"
#include "UI/Private/TextBidi.h"
#include <cstdio>
#include <stdexcept>
#include <array>
#include <fstream>
#include <cstring>
using namespace Beast;
using namespace Beast::UI;
using namespace Beast::UI::Detail;
namespace {
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void Bidi() {
    const auto rtl = VisualBidiRuns(U"אבג"); Check(rtl.size() == 1 && rtl[0].rtl && rtl[0].start == 0 && rtl[0].end == 3, "Hebrew bidi run");
    const auto mixed = VisualBidiRuns(U"abc אבג 123 xyz");
    Check(mixed.size() >= 3, "Mixed directional runs");
    const auto carets = TextCaretPositions("אבג", 24);
    Check(carets.size() == 4 && carets[0] > carets[1] && carets[1] > carets[2] && carets[2] > carets[3], "RTL caret order");
    Check(std::abs(carets[0] - MeasureText("אבג", 24).width) < .01f && std::abs(carets[3]) < .01f, "RTL endpoints");
    for (const auto& [text, font] : std::array<std::pair<const char*, FontId>, 2>{{{"مرحبا", FontId::Arabic}, {"שלום", FontId::Hebrew}}}) {
        const auto run = LayoutGlyphs(text, 32, {}, FontFace::Regular);
        Check(!run.glyphs.empty(), "RTL glyphs present"); for (const auto& glyph : run.glyphs) Check(glyph.font == font && glyph.index != 0, "RTL fallback font glyph");
    }
    // Logical selection crosses a directional boundary and is visually disjoint.
    const auto spans = TextSelectionSpans("abc אבג xyz", 24, 0, 5);
    Check(spans.size() == 2 && spans[0].second < spans[1].first, "Bidi selection disjoint spans");
    const auto combining = TextSelectionSpans("e\u0301x", 24, 0, 2);
    Check(combining.size() == 1 && std::abs(combining[0].second - TextCaretPositions("e\u0301x", 24)[2]) < .01f, "Selection uses cluster caret geometry");
}
void Wrapping() {
    const std::string text = "ffi e\u0301 👍🏽 क्षि مرحبا שלום\r\nnext\n";
    const auto clusters = TextEditor::GraphemeBoundaries(text, true);
    for (float width : {0.f, 1.f, 30.f, 80.f, 1000.f}) {
        const auto lines = WrapText(text, width, 20); std::size_t offset = 0;
        for (const auto& line : lines) {
            Check(line.start == offset && line.end <= line.next, "Contiguous byte ranges");
            Check(std::binary_search(clusters.begin(), clusters.end(), line.start) && std::binary_search(clusters.begin(), clusters.end(), line.end), "No split grapheme");
            Check(std::abs(line.width - MeasureText(text.substr(line.start, line.end - line.start), 20).width) < .01f, "Wrapped line uses shaped metrics"); offset = line.next;
        }
        Check(offset == text.size() && lines.back().start == text.size(), "Trailing newline preserves empty line");
    }
    TextEditor editor("a\r\nb", 4096, true); editor.Select({3, 3}); editor.Delete(true);
    Check(editor.GetValue().text == "ab" && editor.Undo() && editor.GetValue().text == "a\r\nb", "CRLF deletion and undo");
    Check(TextEditor::FromUtf16(u"a\n😀", true) == "a\n😀", "Multiline UTF16 transport");
}
void FieldsAndSemantics() {
    Window window; window.width = window.contentWidth = 360; window.height = window.contentHeight = 600;
    window.scale = 1.5f; window.accessibility = std::make_shared<Accessibility>();
    auto field = std::make_shared<TextField>("אבג", TextFieldOptions{.placeholder = "Name", .maxBytes = 64});
    auto multi = std::make_shared<TextField>("first", TextFieldOptions{.multiline = true, .minLines = 2, .maxLines = 3});
    auto password = std::make_shared<TextField>("secret👍🏽", TextFieldOptions{.placeholder = "Password", .password = true});
    auto invalid = std::make_shared<TextField>("", TextFieldOptions{.placeholder = "Required", .validator = [](const std::string& value) -> std::optional<std::string> { return value.empty() ? std::optional<std::string>("Required") : std::nullopt; }});
    int clicks = 0, changes = 0; auto button = std::make_shared<Button>("Save", [&] { ++clicks; });
    field->SetOnChanged([&](const std::string&) { ++changes; });
    Context context; context.SetRoot(std::make_shared<Column>(std::vector<WidgetPtr>{field, multi, password, invalid, button}));
    context.BuildFrame(window, 0);
    const auto key = [&](Key code) { Event e; e.type = EventType::KeyDown; e.key = code; context.HandleEvent(e, window); };
    key(Key::Tab); field->SetSelection({0, 0}); key(Key::Left); Check(field->GetSelection().caret == 2, "Left arrow moves visually in RTL"); key(Key::Right); Check(field->GetSelection().caret == 0, "Right arrow moves visually in RTL");
    key(Key::Tab); multi->SetSelection({5, 5}); key(Key::Enter); Check(multi->GetText() == "first\n", "Multiline Enter");
    Event character; character.type = EventType::TextInput; character.character = U'Ж'; context.HandleEvent(character, window);
    Check(multi->GetText() == "first\nЖ", "Multiline input"); key(Key::Up); Check(multi->GetSelection().caret < 6, "Vertical caret navigation");
    Check(!invalid->Validate() && invalid->GetError() == "Required", "Validator reports error"); invalid->SetText("valid"); Check(invalid->Validate() && !invalid->GetError(), "Validator clears error");
    password->Edit(TextEditAction::SelectAll); Check(!password->Edit(TextEditAction::Copy), "No password clipboard export");
    context.BuildFrame(window, 0); auto nodes = window.accessibility->Snapshot(); Check(nodes.size() == 5, "Automatic semantic nodes");
    Check(nodes[2].password && nodes[2].value == u"•••••••" && nodes[2].value.find(u"secret") == std::u16string::npos, "Password semantics masked per grapheme");
    Check(nodes[1].multiline && nodes[1].value == u"first\nЖ", "Multiline semantic value");
    Check(std::abs(nodes[0].width - field->GetBounds().width * window.scale) < .01f, "Physical semantic bounds");
    window.accessibility->Enqueue({nodes[4].id, AccessibleAction::Invoke});
    window.accessibility->Enqueue({nodes[0].id, AccessibleAction::SetValue, u"Новое имя"}); context.BuildFrame(window, 0);
    Check(clicks == 1 && changes == 1 && field->GetText() == "Новое имя", "Native semantic actions reach normal callbacks");
    const auto previous = field->GetSelection(); window.accessibility->Enqueue({nodes[0].id, AccessibleAction::SetValue, std::u16string(100, u'a')}); context.BuildFrame(window, 0);
    Check(field->GetText() == "Новое имя" && field->GetSelection() == previous, "Oversize accessible edit atomicity");
    window.accessibility->Enqueue({nodes[4].id, AccessibleAction::Invoke}); context.SetRoot(std::make_shared<Text>("new tree")); context.BuildFrame(window, 0);
    Check(clicks == 1, "Removed widgets reject queued actions");
    window.accessibility->Detach(); Check(window.accessibility->Snapshot().empty() && !window.accessibility->Enqueue({nodes[4].id, AccessibleAction::Invoke}), "Detached semantic provider inert");
}
void Specimen() {
    constexpr int width = 1000, height = 400; std::vector<unsigned char> pixels(width * height * 4, 255);
    const std::array<std::string, 4> lines{"مرحبا بالعالم — Beast 123", "שלום עולם — Beast 123", "abc אבג 123 xyz", "ffi e\u0301 क्षि 👍🏽"};
    for (int row = 0; row < 4; ++row) {
        const auto bitmap = RasterizeText(lines[row], 42, width - 40, {}, 100, FontFace::Regular); const auto& desc = bitmap.texture->GetDesc();
        for (unsigned y = 0; y < desc.height && y < 100; ++y) for (unsigned x = 0; x < desc.width; ++x) {
            const auto offset = ((row * 100 + y) * width + x + 20) * 4;
            for (int c = 0; c < 3; ++c) pixels[offset + c] = static_cast<unsigned char>(255 - desc.pixels[(y * desc.width + x) * 4]);
        }
    }
    std::array<unsigned char, 54> header{}; header[0] = 'B'; header[1] = 'M';
    const auto put = [&](int offset, std::int32_t value) { std::memcpy(header.data() + offset, &value, 4); };
    put(2, 54 + width * height * 4); put(10, 54); put(14, 40); put(18, width); put(22, -height); header[26] = 1; header[28] = 32;
    std::ofstream out("Test/EcoSbor/Intermediate/Verification/bidi-text.bmp", std::ios::binary);
    out.write(reinterpret_cast<const char*>(header.data()), header.size()); out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size())); Check(out.good(), "Specimen written");
}
}
int main() { try { Bidi(); Wrapping(); FieldsAndSemantics(); Specimen(); std::puts("Bidi, wrapping, multiline/password/validation and semantic actions passed."); return 0; } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; } }
