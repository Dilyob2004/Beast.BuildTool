#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include "Platform/Windows/WindowsAccessibility.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <bit>
#pragma comment(lib, "uiautomationcore.lib")
#pragma comment(lib, "oleaut32.lib")

namespace Beast::Detail {
namespace {
    // COM objects retain the bridge, never a Widget or Application pointer.
    class Provider final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IRawElementProviderFragmentRoot, public IInvokeProvider, public IValueProvider, public IScrollProvider {
        std::atomic<ULONG> refs_{1};
        HWND window_; std::shared_ptr<Accessibility> bridge_; int id_;
        std::optional<AccessibleRole> role_; // COM interface support is stable for this object's lifetime.
        std::optional<AccessibleNode> Node() const {
            for (auto& n : bridge_->Snapshot()) if (n.id == id_) return n; return {};
        }
        bool Alive() const { return bridge_->Attached() && IsWindow(window_) && (!id_ || Node().has_value()); }
        HRESULT Queue(AccessibleAction action, std::u16string value = {}) {
            if (!Alive()) return UIA_E_ELEMENTNOTAVAILABLE;
            return bridge_->Enqueue({id_, action, std::move(value)}) ? S_OK : UIA_E_INVALIDOPERATION;
        }
    public:
        Provider(HWND window, std::shared_ptr<Accessibility> bridge, int id) : window_(window), bridge_(std::move(bridge)), id_(id) { if (auto node = Node()) role_ = node->role; }
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
            if (!out) return E_POINTER; *out = nullptr;
            if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
            else if (iid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
            else if (iid == __uuidof(IRawElementProviderFragmentRoot) && id_ == 0) *out = static_cast<IRawElementProviderFragmentRoot*>(this);
            else if (iid == __uuidof(IInvokeProvider) && role_ == AccessibleRole::Button) *out = static_cast<IInvokeProvider*>(this);
            else if (iid == __uuidof(IValueProvider) && role_ == AccessibleRole::TextField) *out = static_cast<IValueProvider*>(this);
            else if (iid == __uuidof(IScrollProvider) && role_ == AccessibleRole::ScrollView) *out = static_cast<IScrollProvider*>(this);
            if (!*out) return E_NOINTERFACE; AddRef(); return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
        ULONG STDMETHODCALLTYPE Release() override { auto count = --refs_; if (!count) delete this; return count; }
        HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override { if (!out) return E_POINTER; *out = ProviderOptions_ServerSideProvider; return S_OK; }
        HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** out) override {
            if (!out) return E_POINTER; *out = nullptr; auto node = Node(); if (!Alive()) return UIA_E_ELEMENTNOTAVAILABLE;
            if (node && pattern == UIA_InvokePatternId && node->role == AccessibleRole::Button) *out = static_cast<IInvokeProvider*>(this);
            if (node && pattern == UIA_ValuePatternId && node->role == AccessibleRole::TextField) *out = static_cast<IValueProvider*>(this);
            if (node && pattern == UIA_ScrollPatternId && node->role == AccessibleRole::ScrollView) *out = static_cast<IScrollProvider*>(this);
            if (*out) AddRef(); return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* out) override {
            if (!out) return E_POINTER; VariantInit(out); if (!Alive()) return UIA_E_ELEMENTNOTAVAILABLE;
            const auto node = Node();
            const auto text = [&](const std::u16string& s) { out->vt = VT_BSTR; out->bstrVal = SysAllocStringLen(reinterpret_cast<const OLECHAR*>(s.data()), static_cast<UINT>(s.size())); return out->bstrVal ? S_OK : E_OUTOFMEMORY; };
            const auto boolean = [&](bool b) { out->vt = VT_BOOL; out->boolVal = b ? VARIANT_TRUE : VARIANT_FALSE; return S_OK; };
            if (property == UIA_ControlTypePropertyId) {
                out->vt = VT_I4; out->lVal = !node ? UIA_PaneControlTypeId : node->role == AccessibleRole::Button ? UIA_ButtonControlTypeId : node->role == AccessibleRole::TextField ? UIA_EditControlTypeId : node->role == AccessibleRole::ScrollView ? UIA_PaneControlTypeId : UIA_TextControlTypeId; return S_OK;
            }
            if (property == UIA_NamePropertyId) return text(node ? node->label : u"Beast UI");
            if (property == UIA_FrameworkIdPropertyId) return text(u"Beast");
            if (property == UIA_HelpTextPropertyId && node) return text(node->error);
            if (property == UIA_IsEnabledPropertyId) return boolean(!node || node->enabled);
            if (property == UIA_IsPasswordPropertyId) return boolean(node && node->password);
            if (property == UIA_HasKeyboardFocusPropertyId) return boolean(node && node->focused);
            if (property == UIA_IsKeyboardFocusablePropertyId) return boolean(node && (node->role == AccessibleRole::Button || node->role == AccessibleRole::TextField));
            if (property == UIA_IsControlElementPropertyId || property == UIA_IsContentElementPropertyId) return boolean(true);
            if (property == UIA_IsOffscreenPropertyId) return boolean(false);
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
            if (!out) return E_POINTER; *out = nullptr; if (!Alive()) return UIA_E_ELEMENTNOTAVAILABLE;
            return id_ ? S_OK : UiaHostProviderFromHwnd(window_, out);
        }
        HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override {
            if (!out) return E_POINTER; *out = nullptr; if (!Alive()) return UIA_E_ELEMENTNOTAVAILABLE;
            auto nodes = bridge_->Snapshot(); int target = -1;
            if (id_ && direction == NavigateDirection_Parent) target = 0;
            if (!id_ && !nodes.empty() && direction == NavigateDirection_FirstChild) target = nodes.front().id;
            if (!id_ && !nodes.empty() && direction == NavigateDirection_LastChild) target = nodes.back().id;
            for (std::size_t n = 0; id_ && n < nodes.size(); ++n) if (nodes[n].id == id_) {
                if (direction == NavigateDirection_NextSibling && n + 1 < nodes.size()) target = nodes[n + 1].id;
                if (direction == NavigateDirection_PreviousSibling && n) target = nodes[n - 1].id;
            }
            if (target >= 0) *out = new Provider(window_, bridge_, target); return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
            if (!out) return E_POINTER; *out = nullptr; if (!id_) return S_OK;
            *out = SafeArrayCreateVector(VT_I4, 0, 2); if (!*out) return E_OUTOFMEMORY;
            LONG index = 0; int value = UiaAppendRuntimeId; SafeArrayPutElement(*out, &index, &value); index = 1; value = id_; SafeArrayPutElement(*out, &index, &value); return S_OK;
        }
        HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
            if (!out) return E_POINTER; *out = {}; if (!Alive()) return UIA_E_ELEMENTNOTAVAILABLE;
            POINT origin{}; ClientToScreen(window_, &origin); const auto node = Node(); RECT client{}; GetClientRect(window_, &client);
            *out = {origin.x + (node ? node->x : 0.0), origin.y + (node ? node->y : 0.0), node ? node->width : double(client.right), node ? node->height : double(client.bottom)}; return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
        HRESULT STDMETHODCALLTYPE SetFocus() override { return Queue(AccessibleAction::Focus); }
        HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override { if (!out) return E_POINTER; *out = nullptr; if (!Alive()) return UIA_E_ELEMENTNOTAVAILABLE; *out = new Provider(window_, bridge_, 0); return S_OK; }
        HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override {
            if (!out) return E_POINTER; *out = nullptr; if (!Alive()) return UIA_E_ELEMENTNOTAVAILABLE;
            POINT origin{}; ClientToScreen(window_, &origin); auto nodes = bridge_->Snapshot();
            for (auto i = nodes.rbegin(); i != nodes.rend(); ++i) if (x >= origin.x + i->x && y >= origin.y + i->y && x < origin.x + i->x + i->width && y < origin.y + i->y + i->height) { *out = new Provider(window_, bridge_, i->id); break; }
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override {
            if (!out) return E_POINTER; *out = nullptr; if (!Alive()) return UIA_E_ELEMENTNOTAVAILABLE;
            for (const auto& n : bridge_->Snapshot()) if (n.focused) { *out = new Provider(window_, bridge_, n.id); break; } return S_OK;
        }
        HRESULT STDMETHODCALLTYPE Invoke() override { return Queue(AccessibleAction::Invoke); }
        HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override {
            auto node = Node(); if (!node) return UIA_E_ELEMENTNOTAVAILABLE; if (node->readOnly || !value) return UIA_E_INVALIDOPERATION;
            std::size_t length = 0; while (length <= 4096 && value[length]) ++length; if (length > 4096) return E_INVALIDARG;
            return Queue(AccessibleAction::SetValue, std::u16string(reinterpret_cast<const char16_t*>(value), length));
        }
        HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override {
            if (!out) return E_POINTER; *out = nullptr; auto node = Node(); if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
            if (node->password) return E_ACCESSDENIED;
            *out = SysAllocStringLen(reinterpret_cast<const OLECHAR*>(node->value.data()), static_cast<UINT>(node->value.size())); return *out ? S_OK : E_OUTOFMEMORY;
        }
        HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override { if (!out) return E_POINTER; auto node = Node(); if (!node) return UIA_E_ELEMENTNOTAVAILABLE; *out = node->readOnly; return S_OK; }
        HRESULT STDMETHODCALLTYPE Scroll(ScrollAmount horizontal, ScrollAmount vertical) override {
            auto node = Node(); if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
            const auto amount = node->horizontal ? horizontal : vertical;
            if ((node->horizontal ? vertical : horizontal) != ScrollAmount_NoAmount) return UIA_E_INVALIDOPERATION;
            if (amount == ScrollAmount_NoAmount) return S_OK;
            return Queue(amount == ScrollAmount_SmallDecrement || amount == ScrollAmount_LargeDecrement ? AccessibleAction::ScrollBackward : AccessibleAction::ScrollForward);
        }
        HRESULT STDMETHODCALLTYPE SetScrollPercent(double horizontal, double vertical) override {
            auto node = Node(); if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
            // SDK builds use /fp:fast. Validate the external COM payload by its
            // IEEE bits before any comparison (including the -1 sentinel).
            const auto finite = [](double value) { return (std::bit_cast<std::uint64_t>(value) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL; };
            if (!finite(horizontal) || !finite(vertical)) return E_INVALIDARG;
            const double percent = node->horizontal ? horizontal : vertical;
            if ((node->horizontal ? vertical : horizontal) != UIA_ScrollPatternNoScroll) return E_INVALIDARG;
            if (percent == UIA_ScrollPatternNoScroll) return S_OK;
            if (!std::isfinite(percent) || percent < 0 || percent > 100) return E_INVALIDARG;
            AccessibilityAction action{id_, AccessibleAction::ScrollTo}; action.scrollPercent = percent; return bridge_->Enqueue(std::move(action)) ? S_OK : UIA_E_INVALIDOPERATION;
        }
        HRESULT ScrollProperty(bool horizontal, bool view, double* out) {
            if (!out) return E_POINTER; auto node = Node(); if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
            const bool active = node->horizontal == horizontal && node->scrollExtent > 0;
            *out = view ? (active ? 100.0 * node->viewport / (node->viewport + node->scrollExtent) : 100.0) : (active ? 100.0 * std::clamp(node->scrollOffset / node->scrollExtent, 0.f, 1.f) : UIA_ScrollPatternNoScroll); return S_OK;
        }
        HRESULT STDMETHODCALLTYPE get_HorizontalScrollPercent(double* out) override { return ScrollProperty(true, false, out); }
        HRESULT STDMETHODCALLTYPE get_VerticalScrollPercent(double* out) override { return ScrollProperty(false, false, out); }
        HRESULT STDMETHODCALLTYPE get_HorizontalViewSize(double* out) override { return ScrollProperty(true, true, out); }
        HRESULT STDMETHODCALLTYPE get_VerticalViewSize(double* out) override { return ScrollProperty(false, true, out); }
        HRESULT STDMETHODCALLTYPE get_HorizontallyScrollable(BOOL* out) override { if (!out) return E_POINTER; auto n = Node(); if (!n) return UIA_E_ELEMENTNOTAVAILABLE; *out = n->horizontal && n->scrollExtent > 0; return S_OK; }
        HRESULT STDMETHODCALLTYPE get_VerticallyScrollable(BOOL* out) override { if (!out) return E_POINTER; auto n = Node(); if (!n) return UIA_E_ELEMENTNOTAVAILABLE; *out = !n->horizontal && n->scrollExtent > 0; return S_OK; }
    };
    class WindowsAccessibility final : public Accessibility {
        HWND window_; int lastFocus_ = 0;
        void Changed(const std::vector<AccessibleNode>& nodes) override {
            if (!UiaClientsAreListening()) return;
            auto* root = new Provider(window_, shared.lock(), 0);
            UiaRaiseStructureChangedEvent(root, StructureChangeType_ChildrenInvalidated, nullptr, 0); root->Release();
            int focus = 0; for (const auto& n : nodes) if (n.focused) focus = n.id;
            if (focus && focus != lastFocus_) { auto* p = new Provider(window_, shared.lock(), focus); UiaRaiseAutomationEvent(p, UIA_AutomationFocusChangedEventId); p->Release(); }
            lastFocus_ = focus;
        }
    public:
        std::weak_ptr<Accessibility> shared;
        std::atomic<bool> requested{false};
        // Reading a UIA tree does not require subscribing to events. Once this
        // window receives a provider request, keep its snapshots available.
        bool Enabled() const override { return requested.load() || UiaClientsAreListening() != FALSE; }
        explicit WindowsAccessibility(HWND window) : window_(window) {}
    };
}
std::shared_ptr<Accessibility> CreateWindowsAccessibility(void* window) {
    auto result = std::make_shared<WindowsAccessibility>(static_cast<HWND>(window)); result->shared = result; return result;
}
IRawElementProviderSimple* CreateWindowsAccessibilityProvider(void* window, const std::shared_ptr<Accessibility>& bridge) {
    if (!bridge || !bridge->Attached() || !IsWindow(static_cast<HWND>(window))) return nullptr;
    if (auto platform = std::dynamic_pointer_cast<WindowsAccessibility>(bridge)) platform->requested = true;
    return new Provider(static_cast<HWND>(window), bridge, 0);
}
long long WindowsAccessibleObject(void* window, std::size_t wParam, long long lParam, const std::shared_ptr<Accessibility>& bridge) {
    if (!bridge || static_cast<LONG>(lParam) != UiaRootObjectId) return 0;
    auto* provider = CreateWindowsAccessibilityProvider(window, bridge); if (!provider) return 0;
    const auto result = UiaReturnRawElementProvider(static_cast<HWND>(window), wParam, static_cast<LPARAM>(lParam), provider); provider->Release(); return result;
}
}
#endif
