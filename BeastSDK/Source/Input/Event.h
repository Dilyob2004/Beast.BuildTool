#pragma once
#include <cstdint>

namespace Beast
{
    enum class Key : unsigned
    {
        Unknown, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        D0, D1, D2, D3, D4, D5, D6, D7, D8, D9,
        Escape, Enter, Space, Tab, Backspace, Left, Right, Up, Down,
        Shift, Control, Alt, Home, End, Delete, Back, Count
    };
    enum class MouseButton : unsigned { Left, Right, Middle, Count };
    enum class EventType
    {
        None, SurfaceCreated, SurfaceDestroyed, Resized, FocusGained, FocusLost,
        Resumed, Suspended, CloseRequested,
        KeyDown, KeyUp, TextInput, MouseMove, MouseDown, MouseUp, MouseWheel,
        TouchDown, TouchMove, TouchUp, TouchCancel, MouseLeave, MouseCancel
    };
    struct Event
    {
        EventType type = EventType::None;
        Key key = Key::Unknown;
        MouseButton button = MouseButton::Left;
        int pointerId = 0;
        float x = 0, y = 0, wheel = 0;
        int width = 0, height = 0;
        char32_t character = 0;
        bool repeat = false;
        // Native monotonic event time in seconds. Zero means unavailable
        // (e.g. synthetic input); consumers then use their local steady clock.
        double timestamp = 0;
    };
}
