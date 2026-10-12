#pragma once
#if defined(_WIN32)
#include "Core/Accessibility.h"
#include <memory>
struct IRawElementProviderSimple;
namespace Beast::Detail {
    std::shared_ptr<Accessibility> CreateWindowsAccessibility(void* window);
    // Caller owns one COM reference. Shared by WM_GETOBJECT and native contract tests.
    IRawElementProviderSimple* CreateWindowsAccessibilityProvider(void* window, const std::shared_ptr<Accessibility>& bridge);
    long long WindowsAccessibleObject(void* window, std::size_t wParam, long long lParam, const std::shared_ptr<Accessibility>& bridge);
}
#endif
