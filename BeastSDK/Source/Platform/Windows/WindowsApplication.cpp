#if defined(_WIN32)
#ifndef UNICODE
#define UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include "Core/Application.h"
#include "Platform/Windows/WindowsAccessibility.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <string>

namespace Beast
{
    namespace
    {
        constexpr wchar_t WindowClassName[] = L"BeastSDKApplicationWindow";
        class WindowsClipboard final : public Clipboard
        {
            HWND window_;
        public:
            explicit WindowsClipboard(HWND window) : window_(window) {}
            std::optional<std::u16string> ReadClipboard() override {
                if (!OpenClipboard(window_)) return {};
                struct Guard { ~Guard() { CloseClipboard(); } } guard;
                auto memory = GetClipboardData(CF_UNICODETEXT); if (!memory) return {};
                const SIZE_T capacity = GlobalSize(memory) / sizeof(wchar_t);
                auto* data = static_cast<const wchar_t*>(GlobalLock(memory)); if (!data) return {};
                SIZE_T count = 0; while (count < capacity && count <= 4096 && data[count]) ++count;
                std::optional<std::u16string> result;
                if (count < capacity && count <= 4096) result = std::u16string(reinterpret_cast<const char16_t*>(data), count);
                GlobalUnlock(memory); return result;
            }
            bool WriteClipboard(const std::u16string& value) override {
                if (value.size() > 4096 || !OpenClipboard(window_)) return false;
                struct Guard { ~Guard() { CloseClipboard(); } } guard;
                auto memory = GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(char16_t)); if (!memory) return false;
                auto* data = static_cast<char16_t*>(GlobalLock(memory));
                if (!data) { GlobalFree(memory); return false; }
                std::copy(value.begin(), value.end(), data); data[value.size()] = 0; GlobalUnlock(memory);
                if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, memory)) { GlobalFree(memory); return false; }
                return true; // Ownership transferred to Windows.
            }
        };
        class FrameWaiter
        {
            HANDLE timer_ = nullptr;
        public:
            FrameWaiter()
            {
                timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
                if (!timer_) timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
            }
            ~FrameWaiter() { if (timer_) CloseHandle(timer_); }
            FrameWaiter(const FrameWaiter&) = delete;
            FrameWaiter& operator=(const FrameWaiter&) = delete;
            void Wait(double milliseconds)
            {
                if (milliseconds <= 0) return;
                if (timer_) {
                    LARGE_INTEGER due{};
                    due.QuadPart = -std::max<LONGLONG>(1, static_cast<LONGLONG>(milliseconds * 10000));
                    if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
                        const auto result = MsgWaitForMultipleObjectsEx(1, &timer_, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                        CancelWaitableTimer(timer_);
                        if (result == WAIT_FAILED) throw std::runtime_error("Frame timer wait failed.");
                        return;
                    }
                }
                MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(std::ceil(milliseconds)), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
        };
        using DpiForWindowFunction = UINT(WINAPI*)(HWND);
        using DpiForSystemFunction = UINT(WINAPI*)();
        using AdjustRectFunction = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        using SetDpiContextFunction = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);

        UINT WindowDpi(HWND window)
        {
            const HMODULE user32 = GetModuleHandleW(L"user32.dll");
            if (window)
                if (auto function = reinterpret_cast<DpiForWindowFunction>(GetProcAddress(user32, "GetDpiForWindow")))
                    return function(window);
            if (auto function = reinterpret_cast<DpiForSystemFunction>(GetProcAddress(user32, "GetDpiForSystem")))
                return function();
            return 96;
        }

        void AdjustClientRect(RECT& rect, DWORD style, DWORD extendedStyle, UINT dpi)
        {
            const auto function = reinterpret_cast<AdjustRectFunction>(
                GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
            if (function)
                function(&rect, style, FALSE, extendedStyle, dpi);
            else
                AdjustWindowRectEx(&rect, style, FALSE, extendedStyle);
        }

        std::wstring ToWide(const std::string& value)
        {
            if (value.empty()) return {};
            const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
            if (length == 0) throw std::runtime_error("Window title must be valid UTF-8.");
            std::wstring result(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, result.data(), length);
            result.pop_back();
            return result;
        }

        Key TranslateKey(WPARAM value)
        {
            if (value >= 'A' && value <= 'Z')
                return static_cast<Key>(static_cast<unsigned>(Key::A) + static_cast<unsigned>(value - 'A'));
            if (value >= '0' && value <= '9')
                return static_cast<Key>(static_cast<unsigned>(Key::D0) + static_cast<unsigned>(value - '0'));
            switch (value)
            {
            case VK_ESCAPE: return Key::Escape;
            case VK_RETURN: return Key::Enter;
            case VK_SPACE: return Key::Space;
            case VK_TAB: return Key::Tab;
            case VK_BACK: return Key::Backspace;
            case VK_LEFT: return Key::Left;
            case VK_RIGHT: return Key::Right;
            case VK_UP: return Key::Up;
            case VK_DOWN: return Key::Down;
            case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return Key::Shift;
            case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return Key::Control;
            case VK_MENU: case VK_LMENU: case VK_RMENU: return Key::Alt;
            case VK_HOME: return Key::Home;
            case VK_END: return Key::End;
            case VK_DELETE: return Key::Delete;
            case VK_BROWSER_BACK: return Key::Back;
            default: return Key::Unknown;
            }
        }

        struct Context
        {
            Application& application;
            WindowDefinition definition;
            HWND handle = nullptr;
            bool ready = false, surfaceCreated = false, minimized = false;
            unsigned capturedButtons = 0;
            wchar_t highSurrogate = 0;
            std::exception_ptr callbackError;

            void Emit(EventType type)
            {
                if (ready) { Event event; event.type = type; application.Dispatch(event); }
            }

            void Resize()
            {
                RECT rect{};
                GetClientRect(handle, &rect);
                auto& window = application.PlatformWindow();
                window.width = rect.right - rect.left;
                window.height = rect.bottom - rect.top;
                window.contentX = window.contentY = 0;
                window.contentWidth = window.width;
                window.contentHeight = window.height;
                window.scale = static_cast<float>(WindowDpi(handle)) / 96.0f;
                if (ready)
                {
                    Event event; event.type = EventType::Resized;
                    event.width = window.width; event.height = window.height;
                    application.Dispatch(event);
                }
            }

            void DestroySurface()
            {
                if (!surfaceCreated) return;
                surfaceCreated = false;
                try { Emit(EventType::SurfaceDestroyed); }
                catch (...)
                {
                    if (!callbackError) callbackError = std::current_exception();
                    Log("Exception while destroying Beast window surface.");
                }
                application.PlatformWindow().nativeHandle = nullptr;
                application.PlatformWindow().visible = false;
            }

            void Text(char32_t value)
            {
                Event event; event.type = EventType::TextInput; event.character = value;
                application.Dispatch(event);
            }

            void Character(wchar_t value)
            {
                if (value >= 0xD800 && value <= 0xDBFF)
                {
                    if (highSurrogate) Text(U'\uFFFD');
                    highSurrogate = value;
                    return;
                }
                if (highSurrogate)
                {
                    const wchar_t high = highSurrogate;
                    highSurrogate = 0;
                    if (value >= 0xDC00 && value <= 0xDFFF)
                    {
                        Text(0x10000u + ((static_cast<char32_t>(high) - 0xD800u) << 10u)
                            + (static_cast<char32_t>(value) - 0xDC00u));
                        return;
                    }
                    Text(U'\uFFFD');
                }
                Text(value >= 0xDC00 && value <= 0xDFFF ? U'\uFFFD' : static_cast<char32_t>(value));
            }
        };

        LRESULT HandleMessage(Context& context, HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
        {
            auto& window = context.application.PlatformWindow();
            switch (message)
            {
            case WM_GETOBJECT:
                if (const auto object = Detail::WindowsAccessibleObject(handle, wParam, lParam, window.accessibility)) return static_cast<LRESULT>(object);
                break;
            case WM_GETMINMAXINFO:
            {
                const UINT dpi = WindowDpi(handle);
                RECT rect{0, 0, MulDiv(std::max(1, context.definition.minWidth), static_cast<int>(dpi), 96),
                    MulDiv(std::max(1, context.definition.minHeight), static_cast<int>(dpi), 96)};
                AdjustClientRect(rect, static_cast<DWORD>(GetWindowLongPtrW(handle, GWL_STYLE)),
                    static_cast<DWORD>(GetWindowLongPtrW(handle, GWL_EXSTYLE)), dpi);
                auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
                limits->ptMinTrackSize = {rect.right - rect.left, rect.bottom - rect.top};
                return 0;
            }
            case WM_SIZE:
            {
                const bool wasMinimized = context.minimized;
                context.minimized = wParam == SIZE_MINIMIZED;
                window.visible = !context.minimized && IsWindowVisible(handle) != FALSE;
                context.Resize();
                if (wasMinimized != context.minimized)
                    context.Emit(context.minimized ? EventType::Suspended : EventType::Resumed);
                return 0;
            }
            case WM_DPICHANGED:
            {
                const RECT& rect = *reinterpret_cast<RECT*>(lParam);
                SetWindowPos(handle, nullptr, rect.left, rect.top, rect.right - rect.left,
                    rect.bottom - rect.top, SWP_NOACTIVATE | SWP_NOZORDER);
                context.Resize();
                return 0;
            }
            case WM_SHOWWINDOW:
                window.visible = wParam != 0 && !context.minimized;
                break;
            case WM_SETFOCUS:
                window.focused = true;
                context.Emit(EventType::FocusGained);
                return 0;
            case WM_KILLFOCUS:
                window.focused = false;
                context.highSurrogate = 0;
                context.Emit(EventType::FocusLost);
                if (GetCapture() == handle) ReleaseCapture();
                return 0;
            case WM_CLOSE:
                context.Emit(EventType::CloseRequested);
                context.application.RequestExit();
                return 0;
            case WM_DESTROY:
                if (window.accessibility) window.accessibility->Detach();
                context.DestroySurface();
                context.application.RequestExit();
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT:
            {
                PAINTSTRUCT paint{};
                HDC device = BeginPaint(handle, &paint);
                if (context.definition.gpuRendering) { EndPaint(handle, &paint); return 0; }
                RECT client{}; GetClientRect(handle, &client);
                SetDCBrushColor(device, RGB(24, 28, 37));
                FillRect(device, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                SetBkMode(device, TRANSPARENT);
                SetTextColor(device, RGB(225, 233, 245));
                client.top = std::max(0L, (client.bottom - 44) / 2);
                DrawTextW(device, L"Beast Engine SDK\nWindow, lifecycle and input are ready.", -1,
                    &client, DT_CENTER | DT_TOP | DT_NOPREFIX);
                EndPaint(handle, &paint);
                return 0;
            }
            default: break;
            }

            if (!context.ready) return DefWindowProcW(handle, message, wParam, lParam);
            Event event;
            const auto inputNow = GetTickCount64();
            const auto inputAge = static_cast<DWORD>(inputNow) - static_cast<DWORD>(GetMessageTime());
            event.timestamp = static_cast<double>(inputNow - inputAge) / 1000.0;
            event.x = static_cast<float>(GET_X_LPARAM(lParam));
            event.y = static_cast<float>(GET_Y_LPARAM(lParam));
            switch (message)
            {
            case WM_KEYDOWN: case WM_SYSKEYDOWN:
            case WM_KEYUP: case WM_SYSKEYUP:
                event.key = TranslateKey(wParam);
                if (event.key == Key::Unknown) break;
                event.type = message == WM_KEYDOWN || message == WM_SYSKEYDOWN ? EventType::KeyDown : EventType::KeyUp;
                event.repeat = event.type == EventType::KeyDown && (lParam & (1LL << 30)) != 0;
                context.application.Dispatch(event);
                if (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)
                    return DefWindowProcW(handle, message, wParam, lParam);
                return 0;
            case WM_CHAR:
                context.Character(static_cast<wchar_t>(wParam));
                return 0;
            case WM_UNICHAR:
                if (wParam == UNICODE_NOCHAR) return TRUE;
                context.Text(wParam <= 0x10FFFF && (wParam < 0xD800 || wParam > 0xDFFF)
                    ? static_cast<char32_t>(wParam) : U'\uFFFD');
                return 0;
            case WM_MOUSEMOVE:
            {
                TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, handle, 0};
                TrackMouseEvent(&tracking);
                event.type = EventType::MouseMove;
                context.application.Dispatch(event);
                return 0;
            }
            case WM_MOUSELEAVE:
                context.Emit(EventType::MouseLeave);
                return 0;
            case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
            case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
            {
                const bool down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN;
                event.button = message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ? MouseButton::Left
                    : message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ? MouseButton::Right : MouseButton::Middle;
                const unsigned bit = 1u << static_cast<unsigned>(event.button);
                if (down) { context.capturedButtons |= bit; SetCapture(handle); }
                else context.capturedButtons &= ~bit;
                event.type = down ? EventType::MouseDown : EventType::MouseUp;
                context.application.Dispatch(event);
                if (!down && context.capturedButtons == 0 && GetCapture() == handle) ReleaseCapture();
                return 0;
            }
            case WM_CAPTURECHANGED:
            {
                const unsigned buttons = context.capturedButtons;
                context.capturedButtons = 0;
                event.type = EventType::MouseCancel;
                event.x = context.application.GetInput().MouseX();
                event.y = context.application.GetInput().MouseY();
                for (unsigned button = 0; button < static_cast<unsigned>(MouseButton::Count); ++button)
                    if ((buttons & (1u << button)) != 0)
                    {
                        event.button = static_cast<MouseButton>(button);
                        context.application.Dispatch(event);
                    }
                return 0;
            }
            case WM_MOUSEWHEEL:
            {
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(handle, &point);
                event.type = EventType::MouseWheel;
                event.x = static_cast<float>(point.x); event.y = static_cast<float>(point.y);
                event.wheel = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
                context.application.Dispatch(event);
                return 0;
            }
            default: break;
            }
            return DefWindowProcW(handle, message, wParam, lParam);
        }

        LRESULT CALLBACK WindowProcedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
        {
            auto* context = reinterpret_cast<Context*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                context = static_cast<Context*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
                context->handle = handle;
                SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
            }
            if (!context) return DefWindowProcW(handle, message, wParam, lParam);
            if (message == WM_NCDESTROY)
            {
                SetWindowLongPtrW(handle, GWLP_USERDATA, 0);
                context->handle = nullptr;
                return DefWindowProcW(handle, message, wParam, lParam);
            }
            try { return HandleMessage(*context, handle, message, wParam, lParam); }
            catch (...)
            {
                if (!context->callbackError) context->callbackError = std::current_exception();
                context->application.RequestExit();
                return 0;
            }
        }
    }

    int Run(Application& application, void* nativeContext)
    {
        (void)nativeContext;
        Context context{application, {}};
        bool started = false;
        int result = 0;
        const auto setDpiContext = reinterpret_cast<SetDpiContextFunction>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext"));
        const DPI_AWARENESS_CONTEXT previousDpi = setDpiContext ? setDpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) : nullptr;
        try
        {
            context.definition = application.Configure();
            const std::wstring title = ToWide(context.definition.title);
            WNDCLASSEXW windowClass{};
            windowClass.cbSize = sizeof(windowClass);
            windowClass.style = CS_OWNDC;
            windowClass.lpfnWndProc = WindowProcedure;
            windowClass.hInstance = GetModuleHandleW(nullptr);
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hIcon = LoadIconW(windowClass.hInstance, MAKEINTRESOURCEW(101));
            if (!windowClass.hIcon) windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
            windowClass.lpszClassName = WindowClassName;
            if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                throw std::runtime_error("Cannot register Beast window class.");
            DWORD style = WS_OVERLAPPEDWINDOW;
            if (!context.definition.resizable) style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
            const UINT dpi = WindowDpi(nullptr);
            RECT rect{0, 0, MulDiv(std::max({1, context.definition.width, context.definition.minWidth}), static_cast<int>(dpi), 96),
                MulDiv(std::max({1, context.definition.height, context.definition.minHeight}), static_cast<int>(dpi), 96)};
            AdjustClientRect(rect, style, WS_EX_APPWINDOW, dpi);
            const HWND handle = CreateWindowExW(WS_EX_APPWINDOW, WindowClassName, title.c_str(), style,
                CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                nullptr, nullptr, windowClass.hInstance, &context);
            if (!handle) throw std::runtime_error("Cannot create Beast window.");
            if (context.callbackError) std::rethrow_exception(context.callbackError);
            application.PlatformWindow().nativeHandle = handle;
            application.PlatformWindow().clipboard = std::make_shared<WindowsClipboard>(handle);
            application.PlatformWindow().accessibility = Detail::CreateWindowsAccessibility(handle);
            context.Resize();
            started = true;
            application.OnStart();
            context.ready = true;
            context.surfaceCreated = true;
            context.Emit(EventType::SurfaceCreated);
            context.Resize();
            ShowWindow(handle, SW_SHOW);
            UpdateWindow(handle);
            context.Emit(EventType::Resumed);
            auto previousFrame = std::chrono::steady_clock::now();
            FrameWaiter frameWaiter;
            while (!application.IsExitRequested())
            {
                application.BeginFrame();
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    if (message.message == WM_QUIT) { application.RequestExit(); break; }
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                    if (application.IsExitRequested()) break;
                }
                if (context.callbackError) std::rethrow_exception(context.callbackError);
                if (application.IsExitRequested()) break;
                const auto now = std::chrono::steady_clock::now();
                const double delta = std::min(0.25, std::chrono::duration<double>(now - previousFrame).count());
                previousFrame = now;
                application.PlatformWindow().frameTimeSeconds = static_cast<double>(GetTickCount64()) / 1000.0;
                if (!context.minimized && application.GetWindow().visible) application.OnUpdate(delta);
                // Present may already have consumed the frame's VSync budget.
                // Sleeping a full 16 ms afterwards halves the effective cadence.
                const double spentMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - now).count();
                frameWaiter.Wait(context.minimized ? 100 : 1000.0 / 60.0 - spentMs);
            }
            if (context.callbackError) std::rethrow_exception(context.callbackError);
        }
        catch (const std::exception& error) { Log(error.what()); result = 1; }
        catch (...) { Log("Unhandled exception in Beast Windows application."); result = 1; }
        context.DestroySurface();
        if (started)
        {
            try { application.OnStop(); }
            catch (...) { Log("Exception during Beast application shutdown."); result = 1; }
        }
        context.ready = false;
        if (context.handle) DestroyWindow(context.handle);
        application.PlatformWindow() = {};
        if (context.callbackError) result = 1;
        if (previousDpi && setDpiContext) setDpiContext(previousDpi);
        return result;
    }
}
#endif
