#include "Launch/ButtonScene.h"
#include "UI/Private/Font.h"
#include "UI/Canvas.h"
#include "RHI/Private/RHIInternal.h"
#include "RHI/Private/OpenGL/OpenGLContext.h"
#include "RHI/Private/OpenGL/OpenGLFunctions.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <source_location>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using namespace Beast;
using namespace Beast::UI;
namespace
{
    void Check(bool value, std::source_location line = std::source_location::current())
    { if (!value) throw std::runtime_error("UI assertion at line " + std::to_string(line.line())); }
    template<class F> void Reject(F&& action, std::source_location line = std::source_location::current())
    {
        bool rejected = false; try { action(); } catch (const std::logic_error&) { rejected = true; }
        Check(rejected, line);
    }
    Window View(float scale = 1)
    {
        Window w; w.width = w.contentWidth = static_cast<int>(600 * scale);
        w.height = w.contentHeight = static_cast<int>(800 * scale); w.scale = scale; return w;
    }
    Point CenterOf(const std::shared_ptr<Button>& b)
    { const auto r = b->GetBounds(); return {r.x + r.width / 2, r.y + r.height / 2}; }
    bool Pointer(Context& ui, const Window& window, EventType type, Point point, int id = 0)
    {
        Event event; event.type = type; event.pointerId = id;
        event.x = window.contentX + point.x * window.scale; event.y = window.contentY + point.y * window.scale;
        return ui.HandleEvent(event, window);
    }
    void KeyEvent(Context& ui, const Window& window, EventType type, Key key, bool repeat = false)
    { Event event; event.type = type; event.key = key; event.repeat = repeat; ui.HandleEvent(event, window); }
    void LayoutAndFont()
    {
        Reject([] { Constraints{5, 2, 0, 100}.Validate(); });
        Reject([] { Text("test", -1); });
        Reject([] { Text(std::string(4097, 'x')); });
        Reject([] { Padding(-1, std::make_shared<Text>("test")); });
        Canvas canvas; RHICommandList commands;
        Reject([&] { canvas.Text({}, "test", 16, {}); });
        canvas.Begin(commands, View(), {});
        Reject([&] { canvas.Text({}, "test", 0, {}); });
        Reject([&] { canvas.Text({}, "test", 16, {2, 0, 0, 1}); });
        Reject([&] { canvas.PushClip({std::numeric_limits<float>::quiet_NaN(), 0, 1, 1}); });
        Reject([&] { canvas.RoundedRect({0, 0, 1, 1}, std::numeric_limits<float>::infinity(), {}); });
        canvas.PushClip({1e20f, 1e20f, 10, 10});
        canvas.Text({}, "outside clip", 16, {1, 1, 1, 1}); canvas.PopClip(); canvas.End();
        auto column = std::make_shared<Column>();
        Reject([&] { column->Add(column); });
        auto child = std::make_shared<Text>("shared"); column->Add(child);
        Reject([&] { Center other(child); });
        Context owner; owner.SetRoot(column);
        Context other; Reject([&] { other.SetRoot(column); });
        owner.SetRoot(nullptr); other.SetRoot(column); other.SetRoot(nullptr);
        column->Clear(); Check(child->Measure({}).width > 0);
        const auto latin = UI::Detail::MeasureText("Beast", 16);
        const auto cyrillic = UI::Detail::MeasureText("Привет", 16);
        Check(latin.width > 20 && cyrillic.width > 20);
        Check(std::abs(UI::Detail::MeasureText("Привет", 32).width - 2 * cyrillic.width) < .01f);
        Check(UI::Detail::MeasureText("\xff\xc0\x80", 16).width > 0);
        auto pixels = UI::Detail::RasterizeText("Привет", 32, 512).texture->GetDesc().pixels;
        std::size_t visible = 0;
        for (std::size_t i = 0; i < pixels.size(); i += 4)
        { Check(pixels[i] == pixels[i + 1] && pixels[i] == pixels[i + 2] && pixels[i] == pixels[i + 3]); visible += pixels[i] != 0; }
        Check(visible > 100);
        ButtonScene scene; auto window = View(); scene.ui.BuildFrame(window, 0);
        const auto before = scene.primary->GetBounds();
        scene.ui.BuildFrame(View(2), 0); const auto scaled = scene.primary->GetBounds();
        Check(before.x == scaled.x && before.y == scaled.y && before.width == scaled.width && before.height == scaled.height);
        Check(before.height >= 48);
        window.width += 60; window.height += 100; window.contentX = 30; window.contentY = 50;
        scene.ui.BuildFrame(window, 0); const auto inset = scene.primary->GetBounds();
        Check(inset.x == before.x && inset.y == before.y);
        const auto center = CenterOf(scene.primary);
        Pointer(scene.ui, window, EventType::MouseDown, center); Pointer(scene.ui, window, EventType::MouseUp, center);
        Check(scene.clicks == 1);
        std::puts("UI constraints, ownership, UTF-8/Cyrillic, premultiplied text, dp and safe-area tests passed.");
    }
    void InputTests()
    {
        ButtonScene scene; const auto window = View(); scene.ui.BuildFrame(window, 0);
        const auto center = CenterOf(scene.primary), off = Point{1, 1};
        Pointer(scene.ui, window, EventType::MouseMove, center); Check(scene.primary->IsHovered());
        Pointer(scene.ui, window, EventType::MouseDown, center); Check(scene.primary->IsPressed() && scene.clicks == 0);
        Pointer(scene.ui, window, EventType::MouseUp, center); Check(scene.clicks == 1 && !scene.primary->IsPressed());
        Pointer(scene.ui, window, EventType::MouseUp, center); Check(scene.clicks == 1);
        Pointer(scene.ui, window, EventType::MouseDown, center);
        Pointer(scene.ui, window, EventType::MouseMove, off); Pointer(scene.ui, window, EventType::MouseUp, off); Check(scene.clicks == 1);
        Pointer(scene.ui, window, EventType::MouseDown, center);
        scene.ui.HandleEvent(Event{EventType::MouseCancel}, window);
        Pointer(scene.ui, window, EventType::MouseUp, center); Check(scene.clicks == 1);
        Pointer(scene.ui, window, EventType::MouseMove, center);
        scene.ui.HandleEvent(Event{EventType::MouseLeave}, window); Check(!scene.primary->IsHovered());
        Pointer(scene.ui, window, EventType::TouchDown, center, 7);
        Check(!Pointer(scene.ui, window, EventType::TouchUp, center, 8)); Check(scene.primary->IsPressed());
        Pointer(scene.ui, window, EventType::TouchCancel, center, 7); Check(scene.clicks == 1 && !scene.primary->IsPressed());
        Pointer(scene.ui, window, EventType::TouchDown, center, 7);
        Pointer(scene.ui, window, EventType::TouchMove, {center.x + 25, center.y}, 7);
        Pointer(scene.ui, window, EventType::TouchMove, center, 7);
        Pointer(scene.ui, window, EventType::TouchUp, center, 7); Check(scene.clicks == 1);
        Pointer(scene.ui, window, EventType::TouchDown, center, 7);
        Pointer(scene.ui, window, EventType::TouchUp, {center.x + 25, center.y}, 7); Check(scene.clicks == 1);
        Pointer(scene.ui, window, EventType::TouchDown, center, 7);
        Pointer(scene.ui, window, EventType::TouchUp, center, 7); Check(scene.clicks == 2);
        for (auto loss : {EventType::FocusLost, EventType::Suspended, EventType::SurfaceDestroyed})
        {
            Pointer(scene.ui, window, EventType::MouseDown, center); scene.ui.HandleEvent(Event{loss}, window);
            Pointer(scene.ui, window, EventType::MouseUp, center); Check(scene.clicks == 2);
        }
        Pointer(scene.ui, window, EventType::MouseDown, center); scene.primary->SetEnabled(false); scene.primary->SetEnabled(true);
        Pointer(scene.ui, window, EventType::MouseMove, center); Pointer(scene.ui, window, EventType::MouseUp, center); Check(scene.clicks == 2);
        scene.ui.HandleEvent(Event{EventType::FocusLost}, window);
        KeyEvent(scene.ui, window, EventType::KeyDown, Key::Tab); Check(scene.primary->IsFocused());
        KeyEvent(scene.ui, window, EventType::KeyDown, Key::Space); Check(scene.primary->IsPressed());
        KeyEvent(scene.ui, window, EventType::KeyDown, Key::Space, true); Check(scene.clicks == 2);
        KeyEvent(scene.ui, window, EventType::KeyUp, Key::Space); Check(scene.clicks == 3);
        KeyEvent(scene.ui, window, EventType::KeyDown, Key::Tab); Check(scene.toggle->IsFocused());
        KeyEvent(scene.ui, window, EventType::KeyDown, Key::Tab); Check(scene.primary->IsFocused() && !scene.disabled->IsFocused());
        KeyEvent(scene.ui, window, EventType::KeyDown, Key::Shift);
        KeyEvent(scene.ui, window, EventType::KeyDown, Key::Tab); Check(scene.toggle->IsFocused());
        KeyEvent(scene.ui, window, EventType::KeyUp, Key::Shift);
        scene.primary->SetEnabled(false);
        Pointer(scene.ui, window, EventType::MouseDown, center); Pointer(scene.ui, window, EventType::MouseUp, center); Check(scene.clicks == 3);
        KeyEvent(scene.ui, window, EventType::KeyDown, Key::Tab); Check(scene.toggle->IsFocused());
        scene.primary->SetEnabled(true);
        Pointer(scene.ui, window, EventType::MouseDown, center);
        scene.ui.BuildFrame(View(2), 0); Pointer(scene.ui, View(2), EventType::MouseUp, center); Check(scene.clicks == 3);
        scene.ui.BuildFrame(window, 0);
        scene.primary->SetOnPressed([&] { scene.ui.SetRoot(std::make_shared<Text>("Rebuilt")); });
        Pointer(scene.ui, window, EventType::MouseDown, center); Pointer(scene.ui, window, EventType::MouseUp, center);
        Check(std::dynamic_pointer_cast<Text>(scene.ui.GetRoot()) != nullptr);
        Check(!scene.primary->IsPressed() && !scene.primary->IsFocused());
        std::puts("UI mouse/touch, capture cancellation, slop, multitouch, disabled, keyboard repeat/focus and reentrant callback tests passed.");
    }
    class CaptureContext final : public Beast::Detail::OpenGLContext
    {
        std::unique_ptr<Beast::Detail::OpenGLContext> actual_;
        PFNGLBLITFRAMEBUFFERPROC blit_;
        PFNGLREADPIXELSPROC read_;
        inline static CaptureContext* active_ = nullptr;
        bool saved_ = false;
        static void APIENTRY Blit(GLint x0, GLint y0, GLint x1, GLint y1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
        {
            auto& self = *active_;
            if (!self.saved_)
            {
                std::vector<unsigned char> pixels(static_cast<std::size_t>(x1) * y1 * 4);
                self.read_(0, 0, x1, y1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                std::size_t drawn = 0;
                for (std::size_t i = 0; i < pixels.size(); i += 4)
                    if (pixels[i] > 40 && pixels[i + 1] > 40) ++drawn;
                Check(drawn > 1000);
                // Actual offscreen GPU pixels, BMP bottom-up BGRA, no UI screenshot API.
                for (std::size_t i = 0; i < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
                BITMAPFILEHEADER file{}; file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
                file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
                BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = x1; info.biHeight = y1;
                info.biPlanes = 1; info.biBitCount = 32; info.biSizeImage = static_cast<DWORD>(pixels.size());
                std::ofstream out("Test/ButtonDemo/Intermediate/Verification/windows-opengl.bmp", std::ios::binary);
                out.write(reinterpret_cast<char*>(&file), sizeof(file)); out.write(reinterpret_cast<char*>(&info), sizeof(info));
                out.write(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size())); Check(bool(out));
                self.saved_ = true;
            }
            self.blit_(x0, y0, x1, y1, dx0, dy0, dx1, dy1, mask, filter);
        }
    public:
        explicit CaptureContext(const Window& window)
        {
            actual_ = Beast::Detail::CreateOpenGLContext(window, {});
            blit_ = reinterpret_cast<PFNGLBLITFRAMEBUFFERPROC>(actual_->Load("glBlitFramebuffer"));
            read_ = reinterpret_cast<PFNGLREADPIXELSPROC>(actual_->LoadBase("glReadPixels")); Check(blit_ && read_); active_ = this;
        }
        void* Load(const char* name) override { return std::strcmp(name, "glBlitFramebuffer") == 0 ? reinterpret_cast<void*>(Blit) : actual_->Load(name); }
        void* LoadBase(const char* name) override { return actual_->LoadBase(name); }
        void MakeCurrent() override { actual_->MakeCurrent(); }
        bool Present() override { return actual_->Present(); }
        void GetSize(int& w, int& h) override { actual_->GetSize(w, h); }
    };
    class GpuTest final : public Beast::Application
    {
        RHIBackend backend_;
        std::unique_ptr<RHI> renderer_;
        ButtonScene scene_;
        int attempts_ = 0;
        void Create()
        {
            if (backend_ == RHIBackend::OpenGL) renderer_ = Beast::Detail::CreateOpenGLRHI(GetWindow(), std::make_unique<CaptureContext>(GetWindow()));
            else renderer_ = RHI::Create(GetWindow(), {backend_, true, true});
        }
    public:
        int frames = 0;
        explicit GpuTest(RHIBackend backend) : backend_(backend) {}
        WindowDefinition Configure() const override { WindowDefinition w; w.width = 560; w.height = 700; w.gpuRendering = true; w.title = "Beast UI tests"; return w; }
        void OnEvent(const Event& event) override
        {
            scene_.ui.HandleEvent(event, GetWindow());
            if (event.type == EventType::SurfaceCreated) Create();
            if (event.type == EventType::SurfaceDestroyed) renderer_.reset();
        }
        void OnUpdate(double delta) override
        {
            Check(++attempts_ < 300);
            if (!scene_.ui.Render(*renderer_, GetWindow(), delta)) return;
            ++frames;
            if (frames == 5)
            { auto p = CenterOf(scene_.primary); Pointer(scene_.ui, GetWindow(), EventType::MouseDown, p); Pointer(scene_.ui, GetWindow(), EventType::MouseUp, p); Check(scene_.clicks == 1); }
            if (frames == 10) Check(SetWindowPos(static_cast<HWND>(GetWindow().nativeHandle), nullptr, 0, 0, 850, 700, SWP_NOMOVE | SWP_NOZORDER));
            if (frames == 15) { renderer_.reset(); Create(); }
            if (frames == 25) RequestExit();
        }
    };
}
int main()
{
    try {
        LayoutAndFont(); InputTests();
        for (auto backend : {RHIBackend::OpenGL, RHIBackend::Vulkan}) { GpuTest test(backend); Check(Run(test) == 0 && test.frames == 25); }
        std::puts("UI GPU tests passed: 25 frames per backend, text update, resize, RHI recreation. Inspect Vulkan validation log.");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
