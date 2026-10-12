#include "UI/Private/TextSegmentation.h"
#include "UI/TextEditing.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
using namespace Beast::UI;
void Check(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
void Conformance(const char* file, bool words) {
    std::ifstream input(file); Check(input.good(), "Missing Unicode test data");
    std::string line; int count = 0, lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber; line = line.substr(0, line.find('#'));
        std::istringstream tokens(line); std::string token;
        std::u32string text; std::vector<std::size_t> expected;
        while (tokens >> token) {
            if (token == "÷") expected.push_back(text.size());
            else if (token != "×") text.push_back(static_cast<char32_t>(std::stoul(token, nullptr, 16)));
        }
        if (expected.empty()) continue;
        const auto actual = words ? Detail::WordBreaks(text) : Detail::GraphemeBreaks(text);
        if (actual != expected) throw std::runtime_error(std::string(file) + ":" + std::to_string(lineNumber));
        ++count;
    }
    Check(count > 500, "Conformance corpus unexpectedly small");
    std::cout << file << ": " << count << " cases passed\n";
}
int main() {
    try {
        Conformance("BeastSDK/ThirdParty/Unicode/17.0.0/GraphemeBreakTest.txt", false);
        Conformance("BeastSDK/ThirdParty/Unicode/17.0.0/WordBreakTest.txt", true);
        for (const std::string cluster : {"e\u0301", "👨‍👩‍👧‍👦", "👍🏽", "🇺🇿", "1️⃣", "\u1100\u1161\u11a8", "\u0915\u094d\u0937"}) {
            TextEditor editor("A" + cluster + "Z");
            Check(TextEditor::GraphemeBoundaries(editor.GetValue().text) == std::vector<std::size_t>{0, 1, 1 + cluster.size(), 2 + cluster.size()}, "Cluster boundaries");
            editor.Move(true); editor.Move(true, true);
            Check(editor.GetValue().selection == TextSelection{1 + cluster.size(), 1}, "Shift movement selects cluster");
            Check(editor.Delete(true) && editor.GetValue().text == "AZ", "Delete selected cluster");
            Check(editor.Undo(), "Undo deletion"); editor.Select({1, 1});
            Check(editor.Delete(false) && editor.GetValue().text == "AZ", "Forward deletion");
            editor.Undo(); editor.Select({1 + cluster.size(), 1 + cluster.size()});
            Check(editor.Delete(true) && editor.GetValue().text == "AZ", "Backward deletion");
        }
        TextEditor editor("e\u0301x");
        Check(editor.ApplyIme({"e\u0301x", {1, 1}, TextSelection{0, 1}}), "IME scalar positions preserved");
        editor.Move(false); Check(editor.GetValue().selection.caret == 3, "Right from inside cluster");
        editor.Select({1, 1}); editor.Move(true); Check(editor.GetValue().selection.caret == 0, "Left from inside cluster");
        editor.Select({1, 1}); Check(editor.Delete(true) && editor.GetValue().text == "x", "Inside cluster deletion");
        Check(editor.Undo() && editor.GetValue().selection.caret == 1, "Undo original scalar caret");
        Check(editor.Delete(false) && editor.GetValue().text == "x", "Forward from inside cluster");
        editor.SetText("e\u0301x"); editor.Select({1, 3});
        Check(editor.Delete(true) && editor.GetValue().text == "x", "Partial scalar selection expands for deletion");
        editor.SetText(""); Check(!editor.Delete(true) && !editor.Delete(false), "Empty deletion");
        editor.Move(true); editor.Move(false); Check(editor.GetValue().selection.caret == 0, "Empty movement");
        std::string longCluster = "a"; for (int i = 0; i < 1000; ++i) longCluster += "\u0301";
        editor.SetText(longCluster); Check(editor.Delete(true) && editor.GetValue().text.empty(), "Long combining run");
        Check(TextEditor::ByteToUtf16("e\u0301", 1) == 1, "UTF16 remains scalar based");
        std::cout << "Grapheme editing tests passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
