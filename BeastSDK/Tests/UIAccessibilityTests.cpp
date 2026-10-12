#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include "Platform/Windows/WindowsAccessibility.h"
#include "UI/UI.h"
#include <cstdio>
#include <stdexcept>
#include <limits>
using namespace Beast;
using namespace Beast::UI;
using Microsoft::WRL::ComPtr;
namespace {
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct TestWindow {
    HWND handle = CreateWindowExW(0, L"STATIC", L"Beast accessibility contract test", WS_OVERLAPPEDWINDOW, 0, 0, 400, 500, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    TestWindow() { Check(handle != nullptr, "Create hidden test HWND"); }
    ~TestWindow() { DestroyWindow(handle); }
};
ComPtr<IRawElementProviderFragment> Next(IRawElementProviderFragment* node, NavigateDirection direction) {
    ComPtr<IRawElementProviderFragment> result;
    Check(SUCCEEDED(node->Navigate(direction, &result)), "Fragment navigation succeeds"); return result;
}
template<class T> ComPtr<T> Pattern(IRawElementProviderFragment* fragment, PATTERNID id) {
    ComPtr<IRawElementProviderSimple> simple; Check(SUCCEEDED(fragment->QueryInterface(IID_PPV_ARGS(&simple))), "Simple provider interface");
    ComPtr<IUnknown> unknown; Check(SUCCEEDED(simple->GetPatternProvider(id, &unknown)) && unknown, "Supported pattern exists");
    ComPtr<T> result; Check(SUCCEEDED(unknown.As(&result)), "Pattern interface"); return result;
}
void Test() {
    TestWindow native;
    Window window; window.nativeHandle = native.handle; window.width = window.contentWidth = 360; window.height = window.contentHeight = 480;
    window.accessibility = Beast::Detail::CreateWindowsAccessibility(native.handle);
    int clicks = 0; auto button = std::make_shared<Button>("Save", [&] { ++clicks; });
    auto field = std::make_shared<TextField>("Initial", TextFieldOptions{.placeholder = "Name"});
    auto password = std::make_shared<TextField>("secret", TextFieldOptions{.placeholder = "Password", .password = true});
    auto readOnly = std::make_shared<TextField>("fixed", TextFieldOptions{.readOnly = true});
    auto disabled = std::make_shared<Button>("Disabled", [] {}); disabled->SetEnabled(false);
    Context ui; ui.SetRoot(std::make_shared<Column>(std::vector<WidgetPtr>{button, field, password, readOnly, disabled}));
    ui.BuildFrame(window, 0);
    Check(Beast::Detail::WindowsAccessibleObject(native.handle, 0, OBJID_CLIENT, window.accessibility) == 0, "Unrelated object requests ignored");
    ComPtr<IRawElementProviderSimple> simple; simple.Attach(Beast::Detail::CreateWindowsAccessibilityProvider(native.handle, window.accessibility));
    Check(simple && window.accessibility->Enabled(), "Provider request enables snapshots without event subscription");
    ui.BuildFrame(window, 0); Check(window.accessibility->Snapshot().size() == 5, "Snapshot available after activation");
    ComPtr<IRawElementProviderFragment> root; Check(SUCCEEDED(simple.As(&root)), "Root fragment");
    ComPtr<IRawElementProviderFragmentRoot> fragmentRoot; Check(SUCCEEDED(simple.As(&fragmentRoot)), "Fragment root interface");
    auto first = Next(root.Get(), NavigateDirection_FirstChild); Check(first != nullptr, "First child");
    auto invoke = Pattern<IInvokeProvider>(first.Get(), UIA_InvokePatternId);
    Check(invoke->Invoke() == S_OK && clicks == 0, "Invoke queues instead of touching widgets from COM thread");
    ui.BuildFrame(window, 0); Check(clicks == 1, "Queued invoke dispatched");
    auto second = Next(first.Get(), NavigateDirection_NextSibling); auto value = Pattern<IValueProvider>(second.Get(), UIA_ValuePatternId);
    BSTR text = nullptr; Check(value->get_Value(&text) == S_OK && std::wstring(text) == L"Initial", "UTF16 value exported"); SysFreeString(text);
    Check(value->SetValue(L"مرحبا") == S_OK, "Value action accepted"); ui.BuildFrame(window, 0); Check(field->GetText() == "مرحبا", "UTF16 edit reaches widget");
    Check(second->SetFocus() == S_OK, "Focus request accepted"); ui.BuildFrame(window, 0); Check(field->IsFocused(), "Focus request applied");
    ComPtr<IRawElementProviderFragment> focus; Check(fragmentRoot->GetFocus(&focus) == S_OK && focus, "Focused virtual element returned");
    UiaRect rect{}; Check(second->get_BoundingRectangle(&rect) == S_OK && rect.width > 0 && rect.height > 0, "Physical screen bounds");
    ComPtr<IRawElementProviderFragment> hit; Check(fragmentRoot->ElementProviderFromPoint(rect.left + 2, rect.top + 2, &hit) == S_OK && hit, "Screen coordinate hit test");
    auto hitValue = Pattern<IValueProvider>(hit.Get(), UIA_ValuePatternId); Check(hitValue->get_Value(&text) == S_OK && std::wstring(text) == L"مرحبا", "Hit test resolves correct field"); SysFreeString(text);
    auto third = Next(second.Get(), NavigateDirection_NextSibling); auto secret = Pattern<IValueProvider>(third.Get(), UIA_ValuePatternId);
    text = nullptr; Check(secret->get_Value(&text) == E_ACCESSDENIED && !text, "Password plaintext inaccessible through UIA");
    auto fourth = Next(third.Get(), NavigateDirection_NextSibling); auto fixed = Pattern<IValueProvider>(fourth.Get(), UIA_ValuePatternId);
    BOOL readonly = FALSE; Check(fixed->get_IsReadOnly(&readonly) == S_OK && readonly && fixed->SetValue(L"changed") == UIA_E_INVALIDOPERATION, "Readonly enforced");
    auto fifth = Next(fourth.Get(), NavigateDirection_NextSibling); auto disabledInvoke = Pattern<IInvokeProvider>(fifth.Get(), UIA_InvokePatternId);
    Check(FAILED(disabledInvoke->Invoke()), "Disabled invoke rejected");
    Check(!Next(fifth.Get(), NavigateDirection_NextSibling), "Last child terminates navigation");
    auto content = std::make_shared<Container>(std::make_shared<Text>("Scrollable"), ContainerStyle{.height = 1200.f});
    auto scroll = std::make_shared<ScrollView>(content); ui.SetRoot(scroll); ui.BuildFrame(window, 0);
    Check(invoke->Invoke() == UIA_E_ELEMENTNOTAVAILABLE, "Removed provider is inert");
    ComPtr<IInvokeProvider> retainedInvoke;
    Check(SUCCEEDED(first.As(&retainedInvoke)) && retainedInvoke->Invoke() == UIA_E_ELEMENTNOTAVAILABLE, "COM interface support survives element removal");
    auto scrollNode = Next(root.Get(), NavigateDirection_FirstChild); auto scrollPattern = Pattern<IScrollProvider>(scrollNode.Get(), UIA_ScrollPatternId);
    double percent = 0; BOOL canScroll = FALSE;
    Check(scrollPattern->get_VerticallyScrollable(&canScroll) == S_OK && canScroll, "Scrollability exported");
    Check(scrollPattern->SetScrollPercent(UIA_ScrollPatternNoScroll, 50) == S_OK, "Scroll percent accepted"); ui.BuildFrame(window, 0);
    Check(scrollPattern->get_VerticalScrollPercent(&percent) == S_OK && std::abs(percent - 50) < .01, "Scroll percent applied");
    Check(scrollPattern->SetScrollPercent(UIA_ScrollPatternNoScroll, std::numeric_limits<double>::quiet_NaN()) == E_INVALIDARG, "Nonfinite scroll rejected");
    Check(scrollPattern->SetScrollPercent(std::numeric_limits<double>::infinity(), 50) == E_INVALIDARG, "Nonfinite inactive axis rejected");
    window.accessibility->Detach();
    Check(simple->GetPropertyValue(UIA_NamePropertyId, nullptr) == E_POINTER, "COM null argument contract");
    Check(root->get_BoundingRectangle(&rect) == UIA_E_ELEMENTNOTAVAILABLE && scrollPattern->get_VerticalScrollPercent(&percent) == UIA_E_ELEMENTNOTAVAILABLE, "Detached providers unavailable");
    Check(!Beast::Detail::CreateWindowsAccessibilityProvider(native.handle, window.accessibility), "Detached bridge cannot be reactivated");
}
}
int main() { try { Test(); std::puts("Windows UIA: activation, navigation, Invoke/Value/Scroll, password protection and lifecycle passed."); return 0; } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; } }
