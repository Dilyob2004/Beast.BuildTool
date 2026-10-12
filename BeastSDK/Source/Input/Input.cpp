#include "Input/Input.h"

namespace Beast
{
    namespace
    {
        void Press(ButtonState& state) { state.pressed |= !state.held; state.held = true; }
        void Release(ButtonState& state) { state.released |= state.held; state.held = false; }
    }
    const ButtonState& Input::GetKey(Key key) const
    {
        static const ButtonState empty{};
        const auto index = static_cast<unsigned>(key);
        return index < keys_.size() ? keys_[index] : empty;
    }
    const ButtonState& Input::GetMouseButton(MouseButton button) const
    {
        static const ButtonState empty{};
        const auto index = static_cast<unsigned>(button);
        return index < buttons_.size() ? buttons_[index] : empty;
    }
    void Input::BeginFrame()
    {
        for (auto& state : keys_) state.pressed = state.released = false;
        for (auto& state : buttons_) state.pressed = state.released = false;
        wheel_ = 0;
    }
    void Input::ReleaseAll()
    {
        for (auto& state : keys_) Release(state);
        for (auto& state : buttons_) Release(state);
        touches_.clear();
    }
    void Input::Apply(const Event& event)
    {
        const auto key = static_cast<unsigned>(event.key);
        const auto button = static_cast<unsigned>(event.button);
        switch (event.type)
        {
        case EventType::KeyDown: if (key && key < keys_.size()) Press(keys_[key]); break;
        case EventType::KeyUp: if (key && key < keys_.size()) Release(keys_[key]); break;
        case EventType::MouseDown:
            if (button < buttons_.size()) Press(buttons_[button]);
            mouseX_ = event.x; mouseY_ = event.y; break;
        case EventType::MouseUp: case EventType::MouseCancel:
            if (button < buttons_.size()) Release(buttons_[button]);
            mouseX_ = event.x; mouseY_ = event.y; break;
        case EventType::MouseMove: mouseX_ = event.x; mouseY_ = event.y; break;
        case EventType::MouseWheel: wheel_ += event.wheel; break;
        case EventType::TouchDown: touches_[event.pointerId] = {event.x, event.y}; break;
        case EventType::TouchMove:
            if (auto point = touches_.find(event.pointerId); point != touches_.end()) point->second = {event.x, event.y};
            break;
        case EventType::TouchUp: case EventType::TouchCancel: touches_.erase(event.pointerId); break;
        case EventType::FocusLost: case EventType::Suspended: case EventType::SurfaceDestroyed: ReleaseAll(); break;
        default: break;
        }
    }
}
