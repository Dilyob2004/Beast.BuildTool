#include "Beast.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using namespace Beast;

void TestInput()
{
    Input input;
    Event event{};
    event.type = EventType::KeyDown; event.key = Key::A;
    input.Apply(event);
    assert(input.GetKey(Key::A).pressed && input.GetKey(Key::A).held);
    input.BeginFrame();
    event.repeat = true; input.Apply(event);
    assert(input.GetKey(Key::A).held && !input.GetKey(Key::A).pressed);
    event.type = EventType::KeyUp; input.Apply(event);
    assert(input.GetKey(Key::A).released && !input.GetKey(Key::A).held);
    input.BeginFrame();
    event.type = EventType::KeyDown; input.Apply(event);
    event.type = EventType::KeyUp; input.Apply(event);
    assert(input.GetKey(Key::A).pressed && input.GetKey(Key::A).released);
    input.BeginFrame();
    assert(!input.GetKey(Key::A).pressed && !input.GetKey(Key::A).released);
    event.type = EventType::TouchDown; event.pointerId = 42; event.x = 10; input.Apply(event);
    event.pointerId = 7; input.Apply(event);
    event.type = EventType::TouchMove; event.pointerId = 42; event.x = 30; input.Apply(event);
    assert(input.GetTouches().at(42).x == 30 && input.GetTouches().size() == 2);
    event.type = EventType::TouchCancel; input.Apply(event);
    assert(input.GetTouches().count(42) == 0 && input.GetTouches().count(7) == 1);
    for (auto loss : {EventType::FocusLost, EventType::Suspended, EventType::SurfaceDestroyed})
    {
        event.type = EventType::KeyDown; input.Apply(event);
        event.type = EventType::MouseDown; input.Apply(event);
        event.type = EventType::TouchDown; input.Apply(event);
        event.type = loss; input.Apply(event);
        assert(!input.GetKey(Key::A).held && input.GetKey(Key::A).released);
        assert(!input.GetMouseButton(MouseButton::Left).held && input.GetTouches().empty());
        input.BeginFrame();
    }
    event.type = EventType::KeyDown; event.key = static_cast<Key>(999); input.Apply(event);
    assert(!input.GetKey(event.key).held);
    event.type = EventType::MouseDown; event.button = static_cast<MouseButton>(999); input.Apply(event);
    assert(!input.GetMouseButton(event.button).held);
    event.type = EventType::MouseWheel; event.wheel = 2; input.Apply(event); input.Apply(event);
    assert(input.WheelDelta() == 4); input.BeginFrame(); assert(input.WheelDelta() == 0);
    Application app;
    event.type = EventType::FocusGained; app.Dispatch(event); assert(app.GetWindow().focused);
    event.type = EventType::FocusLost; app.Dispatch(event); assert(!app.GetWindow().focused);
    event.type = EventType::CloseRequested; app.Dispatch(event); assert(app.IsExitRequested());
}

#if defined(_WIN32)
class WindowTest final : public Application
{
public:
    int starts = 0, stops = 0, surfaces = 0, destroyed = 0, frames = 0, keys = 0;
    bool fail = false;
    void OnStart() override { ++starts; }
    void OnStop() override { ++stops; assert(!GetWindow().HasSurface()); }
    void OnEvent(const Event& event) override
    {
        if (event.type == EventType::SurfaceCreated)
        {
            ++surfaces;
            assert(GetWindow().HasSurface());
            assert(GetWindow().width > 0 && GetWindow().contentWidth == GetWindow().width);
        }
        if (event.type == EventType::SurfaceDestroyed) { ++destroyed; assert(GetWindow().HasSurface()); }
        if (event.type == EventType::KeyDown) ++keys;
    }
    void OnUpdate(double delta) override
    {
        assert(delta >= 0 && delta <= 0.25);
        if (fail) throw std::runtime_error("Expected test exception");
        auto hwnd = static_cast<HWND>(GetWindow().nativeHandle);
        if (++frames == 1)
        {
            PostMessageW(hwnd, WM_KEYDOWN, 'A', 0);
            PostMessageW(hwnd, WM_KEYUP, 'A', 0);
        }
        if (frames == 2)
        {
            assert(keys == 1);
            assert(GetInput().GetKey(Key::A).pressed && GetInput().GetKey(Key::A).released);
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        assert(frames < 5);
    }
};
#endif

int main()
{
    TestInput();
#if defined(_WIN32)
    WindowTest app;
    assert(Run(app) == 0);
    assert(app.starts == 1 && app.stops == 1 && app.surfaces == 1 && app.destroyed == 1);
    WindowTest failure;
    failure.fail = true;
    assert(Run(failure) == 1);
    assert(failure.starts == 1 && failure.stops == 1 && failure.destroyed == 1);
#endif
    std::puts("Beast core and Windows lifecycle tests passed.");
}
