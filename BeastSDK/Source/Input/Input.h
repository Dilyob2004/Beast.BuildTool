#pragma once
#include "Input/Event.h"
#include <array>
#include <unordered_map>

namespace Beast
{
    struct ButtonState { bool held = false, pressed = false, released = false; };
    struct TouchPoint { float x = 0, y = 0; };
    class Input
    {
    public:
        const ButtonState& GetKey(Key key) const;
        const ButtonState& GetMouseButton(MouseButton button) const;
        const std::unordered_map<int, TouchPoint>& GetTouches() const { return touches_; }
        float MouseX() const { return mouseX_; }
        float MouseY() const { return mouseY_; }
        float WheelDelta() const { return wheel_; }
        // Platform loop: clear transient state before pumping each batch of events.
        void BeginFrame();
        void Apply(const Event& event);
        void ReleaseAll();
    private:
        std::array<ButtonState, static_cast<unsigned>(Key::Count)> keys_{};
        std::array<ButtonState, static_cast<unsigned>(MouseButton::Count)> buttons_{};
        std::unordered_map<int, TouchPoint> touches_;
        float mouseX_ = 0, mouseY_ = 0, wheel_ = 0;
    };
}
