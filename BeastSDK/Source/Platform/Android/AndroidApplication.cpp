#if defined(__ANDROID__)
#include "Core/Application.h"
#include "Core/Trace.h"
#include <android_native_app_glue.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/native_window.h>
#include <android/choreographer.h>
#include "Platform/Android/FrameClock.h"
#include "Platform/Android/SafeArea.h"
#include "Platform/Android/AndroidTextInput.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>

namespace Beast
{
    namespace
    {
        struct State
        {
            Application& application;
            android_app* native;
            bool resumed = false;
            bool finishing = false;
            bool failed = false;
            bool gpuRendering = false;
            int mouseButtons = 0;
            bool framePending = false, frameReady = false;
            std::int64_t frameTime = 0;
            Detail::FrameClock frameClock;
            JNIEnv* insetsEnv = nullptr;
            jmethodID insetsMethod = nullptr;
            jlong safeInsets = -1;
            int availableBottom = -1;
        };

        // Callbacks run on this same looper thread. Epoch tokens contain no
        // pointer to stack State, so late callbacks after stop/restart are safe.
        thread_local State* frameOwner = nullptr;
        thread_local std::uintptr_t frameEpoch = 0;
        void ResetFrames(State& state)
        {
            ++frameEpoch; state.framePending = state.frameReady = false;
            state.frameClock.Reset();
        }
        void OnFrame(std::int64_t time, void* token)
        {
            Detail::TraceScope trace("Beast.Android.Vsync");
            if (!frameOwner || reinterpret_cast<std::uintptr_t>(token) != frameEpoch) return;
            frameOwner->framePending = false; frameOwner->frameReady = true;
            frameOwner->frameTime = time;
            Detail::TraceCounter("Beast.Android.VsyncTimeNs", time);
        }

        void Fail(State& state, const char* message)
        {
            Log(message);
            state.failed = true;
            state.application.RequestExit();
        }

        void Dispatch(State& state, EventType type)
        {
            Event event{};
            event.type = type;
            state.application.Dispatch(event);
        }

        void UpdateDimensions(State& state, bool notify = true)
        {
            auto& window = state.application.PlatformWindow();
            if (window.nativeHandle)
            {
                auto* native = static_cast<ANativeWindow*>(window.nativeHandle);
                window.width = std::max(0, ANativeWindow_getWidth(native));
                window.height = std::max(0, ANativeWindow_getHeight(native));
            }
            const int density = AConfiguration_getDensity(state.native->config);
            window.scale = density > 0 && density < ACONFIGURATION_DENSITY_ANY ? density / 160.0f : 1.0f;
            pthread_mutex_lock(&state.native->mutex);
            const ARect content = state.native->contentRect;
            pthread_mutex_unlock(&state.native->mutex);
            window.contentX = std::clamp(content.left, 0, window.width);
            window.contentY = std::clamp(content.top, 0, window.height);
            // Before Android reports insets, the content area is the whole surface.
            const bool hasContentRect = content.right > content.left && content.bottom > content.top;
            window.contentWidth = hasContentRect ? std::clamp(content.right - window.contentX, 0, window.width - window.contentX) : window.width;
            window.contentHeight = hasContentRect ? std::clamp(content.bottom - window.contentY, 0, window.height - window.contentY) : window.height;
            if (state.safeInsets != -1)
                Detail::ApplySafeInsets(window, static_cast<std::uint64_t>(state.safeInsets));
            window.imeBottom = 0;
            if (state.availableBottom >= 0) {
                const int bottom = std::clamp(state.availableBottom, window.contentY, window.contentY + window.contentHeight);
                window.imeBottom = std::max(1, window.contentY + window.contentHeight - bottom);
                window.contentHeight = bottom - window.contentY;
            }
            if (!notify) return;
            Event event{};
            event.type = EventType::Resized;
            event.width = window.width;
            event.height = window.height;
            state.application.Dispatch(event);
        }

        // Temporary surface diagnostic until the GPU renderer owns presentation.
        // Posting a real buffer also allows Android to dismiss its startup splash.
        void PresentDiagnosticFrame(ANativeWindow* window)
        {
            if (!window) return;
            if (ANativeWindow_setBuffersGeometry(window, 0, 0, WINDOW_FORMAT_RGBA_8888) != 0) return;
            ANativeWindow_Buffer buffer{};
            if (ANativeWindow_lock(window, &buffer, nullptr) != 0) return;
            auto* pixels = static_cast<std::uint32_t*>(buffer.bits);
            for (int y = 0; y < buffer.height; ++y)
                std::fill_n(pixels + static_cast<std::size_t>(y) * buffer.stride, buffer.width, 0xFF30251Cu);
            ANativeWindow_unlockAndPost(window);
        }

        void DestroySurface(State& state)
        {
            ResetFrames(state);
            auto& window = state.application.PlatformWindow();
            if (!window.HasSurface()) return;
            // Deliver this while the borrowed handle is still valid. The glue
            // acknowledges TERM_WINDOW only after this command handler returns.
            try { Dispatch(state, EventType::SurfaceDestroyed); }
            catch (...) { window.nativeHandle = nullptr; window.visible = false; throw; }
            window.nativeHandle = nullptr;
            window.visible = false;
        }

        void OnCommand(android_app* native, int32_t command)
        {
            auto& state = *static_cast<State*>(native->userData);
            auto& window = state.application.PlatformWindow();
            try
            {
                switch (command)
                {
                case APP_CMD_INIT_WINDOW:
                    if (window.HasSurface()) DestroySurface(state);
                    window.nativeHandle = native->window;
                    window.visible = state.resumed && window.HasSurface();
                    if (window.HasSurface())
                    {
                        if (!state.gpuRendering) PresentDiagnosticFrame(native->window);
                        // Dimensions and density are available to SurfaceCreated.
                        UpdateDimensions(state, false);
                        Dispatch(state, EventType::SurfaceCreated);
                        UpdateDimensions(state);
                    }
                    break;
                case APP_CMD_TERM_WINDOW:
                    DestroySurface(state);
                    break;
                case APP_CMD_WINDOW_REDRAW_NEEDED:
                    if (!state.gpuRendering) PresentDiagnosticFrame(native->window);
                    else UpdateDimensions(state);
                    break;
                case APP_CMD_WINDOW_RESIZED:
                case APP_CMD_CONFIG_CHANGED:
                case APP_CMD_CONTENT_RECT_CHANGED:
                    UpdateDimensions(state);
                    break;
                case APP_CMD_RESUME:
                    state.resumed = true;
                    window.visible = window.HasSurface();
                    Dispatch(state, EventType::Resumed);
                    break;
                case APP_CMD_PAUSE:
                case APP_CMD_STOP:
                    ResetFrames(state);
                    window.visible = false;
                    state.mouseButtons = 0;
                    if (state.resumed)
                    {
                        state.resumed = false;
                        Dispatch(state, EventType::Suspended);
                    }
                    break;
                case APP_CMD_GAINED_FOCUS:
                    window.focused = true;
                    Dispatch(state, EventType::FocusGained);
                    break;
                case APP_CMD_LOST_FOCUS:
                    ResetFrames(state);
                    window.focused = false;
                    state.mouseButtons = 0;
                    Dispatch(state, EventType::FocusLost);
                    break;
                case APP_CMD_DESTROY:
                    DestroySurface(state);
                    break;
                default:
                    break;
                }
            }
            catch (const std::exception& error) { Fail(state, error.what()); }
            catch (...) { Fail(state, "Unhandled exception in Android lifecycle callback."); }
        }

        Key TranslateKey(int code)
        {
            if (code >= AKEYCODE_A && code <= AKEYCODE_Z)
                return static_cast<Key>(static_cast<unsigned>(Key::A) + code - AKEYCODE_A);
            if (code >= AKEYCODE_0 && code <= AKEYCODE_9)
                return static_cast<Key>(static_cast<unsigned>(Key::D0) + code - AKEYCODE_0);
            switch (code)
            {
            case AKEYCODE_ESCAPE: return Key::Escape;
            case AKEYCODE_ENTER: case AKEYCODE_NUMPAD_ENTER: return Key::Enter;
            case AKEYCODE_SPACE: return Key::Space;
            case AKEYCODE_TAB: return Key::Tab;
            case AKEYCODE_DEL: return Key::Backspace;
            case AKEYCODE_FORWARD_DEL: return Key::Delete;
            case AKEYCODE_DPAD_LEFT: return Key::Left;
            case AKEYCODE_DPAD_RIGHT: return Key::Right;
            case AKEYCODE_DPAD_UP: return Key::Up;
            case AKEYCODE_DPAD_DOWN: return Key::Down;
            case AKEYCODE_SHIFT_LEFT: case AKEYCODE_SHIFT_RIGHT: return Key::Shift;
            case AKEYCODE_CTRL_LEFT: case AKEYCODE_CTRL_RIGHT: return Key::Control;
            case AKEYCODE_ALT_LEFT: case AKEYCODE_ALT_RIGHT: return Key::Alt;
            case AKEYCODE_MOVE_HOME: return Key::Home;
            case AKEYCODE_MOVE_END: return Key::End;
            case AKEYCODE_BACK: return Key::Back;
            default: return Key::Unknown;
            }
        }

        // Hardware keyboard character translation; software keyboard/IME is a
        // separate future text-input service, not emulated by key-code mapping.
        char32_t TranslateCharacter(State& state, AInputEvent* input)
        {
            JavaVM* vm = state.native->activity->vm;
            JNIEnv* env = nullptr;
            const jint status = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
            const bool attach = status == JNI_EDETACHED;
            if (attach && vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return 0;
            if (!env) return 0;
            jclass keyClass = env->FindClass("android/view/KeyEvent");
            jmethodID constructor = keyClass ? env->GetMethodID(keyClass, "<init>", "(II)V") : nullptr;
            jmethodID unicode = constructor ? env->GetMethodID(keyClass, "getUnicodeChar", "(I)I") : nullptr;
            jobject key = unicode ? env->NewObject(keyClass, constructor, AKeyEvent_getAction(input), AKeyEvent_getKeyCode(input)) : nullptr;
            const jint character = key ? env->CallIntMethod(key, unicode, AKeyEvent_getMetaState(input)) : 0;
            const bool exception = env->ExceptionCheck();
            if (exception) env->ExceptionClear();
            if (key) env->DeleteLocalRef(key);
            if (keyClass) env->DeleteLocalRef(keyClass);
            if (attach) vm->DetachCurrentThread();
            return !exception && character > 0 && character <= 0x10FFFF ? static_cast<char32_t>(character) : 0;
        }

        void DispatchPointer(State& state, AInputEvent* input, std::size_t index, EventType type)
        {
            Event event{};
            event.type = type;
            event.pointerId = AMotionEvent_getPointerId(input, index);
            event.x = AMotionEvent_getX(input, index);
            event.y = AMotionEvent_getY(input, index);
            event.timestamp = AMotionEvent_getEventTime(input) / 1e9;
            state.application.Dispatch(event);
        }

        int32_t OnInput(android_app* native, AInputEvent* input)
        {
            auto& state = *static_cast<State*>(native->userData);
            try
            {
                if (AInputEvent_getType(input) == AINPUT_EVENT_TYPE_KEY)
                {
                    const int code = AKeyEvent_getKeyCode(input);
                    // Preserve Android's normal volume/media/system behavior.
                    if (code == AKEYCODE_VOLUME_UP || code == AKEYCODE_VOLUME_DOWN || code == AKEYCODE_VOLUME_MUTE)
                        return 0;
                    const int action = AKeyEvent_getAction(input);
                    if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP) return 0;
                    Event event{};
                    event.type = action == AKEY_EVENT_ACTION_DOWN ? EventType::KeyDown : EventType::KeyUp;
                    event.key = TranslateKey(code);
                    event.repeat = AKeyEvent_getRepeatCount(input) > 0;
                    state.application.Dispatch(event);
                    // Back policy lives in Application::Dispatch/OnBack; never
                    // force a close on KeyUp after a scene consumed navigation.
                    if (action == AKEY_EVENT_ACTION_DOWN && code != AKEYCODE_BACK)
                    {
                        const char32_t character = TranslateCharacter(state, input);
                        if (character)
                        {
                            event.type = EventType::TextInput;
                            event.character = character;
                            state.application.Dispatch(event);
                            return 1;
                        }
                    }
                    return event.key != Key::Unknown ? 1 : 0;
                }
                if (AInputEvent_getType(input) != AINPUT_EVENT_TYPE_MOTION) return 0;
                const int action = AMotionEvent_getAction(input);
                const int maskedAction = action & AMOTION_EVENT_ACTION_MASK;
                const int source = AInputEvent_getSource(input);
                const std::size_t count = AMotionEvent_getPointerCount(input);
                if ((source & AINPUT_SOURCE_MOUSE) == AINPUT_SOURCE_MOUSE)
                {
                    if (count == 0) return 0;
                    Event event{};
                    event.type = maskedAction == AMOTION_EVENT_ACTION_HOVER_EXIT ? EventType::MouseLeave : EventType::MouseMove;
                    event.x = AMotionEvent_getX(input, 0);
                    event.y = AMotionEvent_getY(input, 0);
                    event.timestamp = AMotionEvent_getEventTime(input) / 1e9;
                    state.application.Dispatch(event);
                    const int buttons = maskedAction == AMOTION_EVENT_ACTION_CANCEL ? 0 : AMotionEvent_getButtonState(input);
                    const int flags[] = { AMOTION_EVENT_BUTTON_PRIMARY, AMOTION_EVENT_BUTTON_SECONDARY, AMOTION_EVENT_BUTTON_TERTIARY };
                    for (unsigned index = 0; index < 3; ++index)
                    {
                        if ((buttons & flags[index]) == (state.mouseButtons & flags[index])) continue;
                        event.type = maskedAction == AMOTION_EVENT_ACTION_CANCEL ? EventType::MouseCancel :
                            (buttons & flags[index]) ? EventType::MouseDown : EventType::MouseUp;
                        event.button = static_cast<MouseButton>(index);
                        state.application.Dispatch(event);
                    }
                    state.mouseButtons = buttons;
                    if (maskedAction == AMOTION_EVENT_ACTION_SCROLL)
                    {
                        event.type = EventType::MouseWheel;
                        event.wheel = AMotionEvent_getAxisValue(input, AMOTION_EVENT_AXIS_VSCROLL, 0);
                        state.application.Dispatch(event);
                    }
                    return 1;
                }
                if ((source & AINPUT_SOURCE_TOUCHSCREEN) != AINPUT_SOURCE_TOUCHSCREEN &&
                    (source & AINPUT_SOURCE_STYLUS) != AINPUT_SOURCE_STYLUS) return 0;
                if (maskedAction == AMOTION_EVENT_ACTION_CANCEL)
                {
                    const auto touches = state.application.GetInput().GetTouches();
                    for (const auto& touch : touches)
                    {
                        Event event{};
                        event.type = EventType::TouchCancel;
                        event.pointerId = touch.first;
                        event.x = touch.second.x;
                        event.y = touch.second.y;
                        state.application.Dispatch(event);
                    }
                }
                else if (maskedAction == AMOTION_EVENT_ACTION_MOVE)
                {
                    for (std::size_t index = 0; index < count; ++index)
                        DispatchPointer(state, input, index, EventType::TouchMove);
                }
                else if (maskedAction == AMOTION_EVENT_ACTION_DOWN || maskedAction == AMOTION_EVENT_ACTION_POINTER_DOWN ||
                         maskedAction == AMOTION_EVENT_ACTION_UP || maskedAction == AMOTION_EVENT_ACTION_POINTER_UP)
                {
                    const std::size_t index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
                    if (index < count)
                        DispatchPointer(state, input, index,
                            maskedAction == AMOTION_EVENT_ACTION_DOWN || maskedAction == AMOTION_EVENT_ACTION_POINTER_DOWN
                                ? EventType::TouchDown : EventType::TouchUp);
                }
                return 1;
            }
            catch (const std::exception& error) { Fail(state, error.what()); }
            catch (...) { Fail(state, "Unhandled exception in Android input callback."); }
            // The glue must still finish the event even when application code failed.
            return 1;
        }
    }

    int Run(Application& application, void* nativeContext)
    {
        auto* native = static_cast<android_app*>(nativeContext);
        if (!native || !native->activity) { Log("Android Run requires an android_app context."); return 1; }
        State state{application, native};
        JavaVM* vm = native->activity->vm;
        const bool attachedForInsets = vm->GetEnv(reinterpret_cast<void**>(&state.insetsEnv), JNI_VERSION_1_6) == JNI_EDETACHED;
        if (attachedForInsets && vm->AttachCurrentThread(&state.insetsEnv, nullptr) != JNI_OK) state.insetsEnv = nullptr;
        if (state.insetsEnv)
        {
            application.PlatformWindow().textInput = Detail::CreateAndroidTextInput(state.insetsEnv, native->activity->clazz);
            application.PlatformWindow().clipboard = application.PlatformWindow().textInput;
            application.PlatformWindow().accessibility = Detail::AndroidAccessibility(application.PlatformWindow().textInput);
            auto activityClass = state.insetsEnv->GetObjectClass(native->activity->clazz);
            state.insetsMethod = state.insetsEnv->GetMethodID(activityClass, "getBeastSafeInsets", "()J");
            // Older project Activities retain the native contentRect fallback.
            if (state.insetsEnv->ExceptionCheck()) state.insetsEnv->ExceptionClear();
            state.insetsEnv->DeleteLocalRef(activityClass);
        }
        frameOwner = &state; ++frameEpoch;
        auto* choreographer = AChoreographer_getInstance();
        if (!choreographer) { frameOwner = nullptr; if (attachedForInsets && state.insetsEnv) vm->DetachCurrentThread(); Log("Android Choreographer unavailable."); return 1; }
        native->userData = &state;
        native->onAppCmd = OnCommand;
        native->onInputEvent = OnInput;
        try
        {
            const auto definition = application.Configure();
            state.gpuRendering = definition.gpuRendering;
            Log(definition.title.c_str());
            application.OnStart();
        }
        catch (const std::exception& error) { Fail(state, error.what()); }
        catch (...) { Fail(state, "Unhandled exception starting Android application."); }

        application.BeginFrame();
        while (!native->destroyRequested)
        {
            if (application.IsExitRequested() && !state.finishing)
            {
                state.finishing = true;
                ResetFrames(state);
                ANativeActivity_finish(native->activity);
            }
            const auto& window = application.GetWindow();
            const bool active = state.resumed && window.focused && window.HasSurface() && !state.finishing;
            // Native lifecycle/input events wake this wait. No busy loop while
            // backgrounded, unfocused, waiting for a surface, or finishing.
            if (active && !state.framePending && !state.frameReady)
            {
                Detail::TraceScope trace("Beast.Android.RequestVsync");
                state.framePending = true;
                AChoreographer_postFrameCallback64(choreographer, OnFrame, reinterpret_cast<void*>(++frameEpoch));
            }
            int timeout = state.frameReady ? 0 : -1;
            for (int batch = 0; batch < 64 && !native->destroyRequested; ++batch)
            {
                int events = 0;
                android_poll_source* source = nullptr;
                const int result = ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&source));
                if (result == ALOOPER_POLL_ERROR)
                {
                    Fail(state, "Android looper failed.");
                    ANativeActivity_finish(native->activity);
                    native->destroyRequested = 1;
                    break;
                }
                if (result >= 0 && source) source->process(native, source);
                if (result == ALOOPER_POLL_TIMEOUT || result == ALOOPER_POLL_WAKE) break;
                timeout = 0;
            }
            if (state.frameReady && !native->destroyRequested && !application.IsExitRequested() && state.resumed && window.focused && window.HasSurface())
            {
                Detail::TraceScope trace("Beast.Android.Frame");
                state.frameReady = false;
                const double delta = state.frameClock.Step(state.frameTime);
                application.PlatformWindow().frameTimeSeconds = state.frameTime / 1e9;
                // Request before rendering, not after potentially expensive work:
                // the next callback remains single-pending on this same looper.
                if (!state.framePending) {
                    Detail::TraceScope requestTrace("Beast.Android.RequestVsync");
                    state.framePending = true;
                    AChoreographer_postFrameCallback64(choreographer, OnFrame, reinterpret_cast<void*>(++frameEpoch));
                }
                try
                {
                    if (state.insetsMethod)
                    {
                        const jlong margins = state.insetsEnv->CallLongMethod(native->activity->clazz, state.insetsMethod);
                        if (state.insetsEnv->ExceptionCheck()) state.insetsEnv->ExceptionClear();
                        else if (margins != state.safeInsets)
                        {
                            state.safeInsets = margins;
                            UpdateDimensions(state);
                        }
                    }
                    const int bottom = Detail::AndroidAvailableBottom(application.PlatformWindow().textInput);
                    if (bottom != state.availableBottom) { state.availableBottom = bottom; UpdateDimensions(state); }
                    application.OnUpdate(delta);
                }
                catch (const std::exception& error) { Fail(state, error.what()); }
                catch (...) { Fail(state, "Unhandled exception updating Android application."); }
                application.BeginFrame(); // Preserve input edges until an actual frame consumes them.
            }
        }
        frameOwner = nullptr; ++frameEpoch;
        try { DestroySurface(state); }
        catch (const std::exception& error) { Fail(state, error.what()); }
        catch (...) { Fail(state, "Unhandled exception destroying Android surface."); }
        try { application.OnStop(); }
        catch (const std::exception& error) { Fail(state, error.what()); }
        catch (...) { Fail(state, "Unhandled exception stopping Android application."); }
        native->onAppCmd = nullptr;
        native->onInputEvent = nullptr;
        native->userData = nullptr;
        if (application.PlatformWindow().accessibility) application.PlatformWindow().accessibility->Detach();
        application.PlatformWindow().accessibility.reset();
        application.PlatformWindow().textInput.reset();
        application.PlatformWindow().clipboard.reset();
        if (attachedForInsets && state.insetsEnv) vm->DetachCurrentThread();
        return state.failed ? 1 : 0;
    }
}
#endif
