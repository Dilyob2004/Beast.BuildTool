#include "Launch/EcoScene.h"
#include "RHI/Private/RHIInternal.h"
#include "RHI/Private/OpenGL/OpenGLContext.h"
#include "RHI/Private/OpenGL/OpenGLFunctions.h"
#include <source_location>
#include <cstdio>
#include <cstring>
#include <fstream>
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
    void Check(bool condition, std::source_location at = std::source_location::current())
    { if (!condition) throw std::runtime_error("Eco assertion at line " + std::to_string(at.line())); }
    template<class F> void Reject(F action) { bool rejected = false; try { action(); } catch (const std::logic_error&) { rejected = true; } Check(rejected); }
    Window View(int width = 411, int height = 850, float scale = 1)
    { Window w; w.width = w.contentWidth = width; w.height = w.contentHeight = height; w.scale = scale; return w; }
    Point Center(const WidgetPtr& widget) { const auto r = widget->GetBounds(); return {r.x + r.width / 2, r.y + r.height / 2}; }
    void Pointer(Context& ui, const Window& w, EventType type, Point p, int id = 0)
    { Event e; e.type = type; e.x = w.contentX + p.x * w.scale; e.y = w.contentY + p.y * w.scale; e.pointerId = id; ui.HandleEvent(e, w); }
    void Click(Context& ui, const Window& w, const WidgetPtr& widget)
    { const auto p = Center(widget); Pointer(ui, w, EventType::MouseDown, p); Pointer(ui, w, EventType::MouseUp, p); ui.BuildFrame(w, 0); }
    void TextTests()
    {
        Paragraph text("Переработка бумаги и пластика\nВторая строка");
        const auto a = text.Measure(Constraints::Loose({300, 1000})), b = text.Measure(Constraints::Loose({90, 1000})); Check(b.height > a.height);
        ParagraphStyle style; style.maxLines = 1; style.ellipsis = true;
        Paragraph shortText("Длинное описание переработки", style); Check(shortText.Measure(Constraints::Loose({80, 1000})).height < 30);
        ParagraphStyle bold; bold.face = FontFace::Bold; Paragraph strong("ЭкоСбор", bold);
        Check(strong.Measure({}).width > 0); Reject([] { Icon invalid(0x10ffff, 24, {1, 1, 1, 1}); });
        Reject([] { ParagraphStyle bad; bad.lineHeight = 0; Paragraph invalid("bad", bad); });
        std::puts("Paragraph wrap/newline/ellipsis, embedded bold and icon validation passed.");
    }
    void ScrollTests()
    {
        auto w = View(320, 300); Context ui; int clicked = 0;
        auto button = std::make_shared<Button>("Scroll child", [&] { ++clicked; });
        auto column = std::make_shared<Column>(); column->Add(button);
        for (int i = 0; i < 30; ++i) column->Add(std::make_shared<Text>("Item " + std::to_string(i)));
        auto scroll = std::make_shared<ScrollView>(column); ui.SetRoot(scroll); ui.BuildFrame(w, 0); Check(scroll->GetExtent() > 0);
        const auto p = Center(button); Pointer(ui, w, EventType::TouchDown, p, 2);
        Pointer(ui, w, EventType::TouchMove, {p.x, p.y - 100}, 3); Check(scroll->GetOffset() == 0); // unrelated pointer
        Pointer(ui, w, EventType::TouchMove, {p.x, p.y - 70}, 2); Check(scroll->GetOffset() > 0 && !button->IsPressed());
        Pointer(ui, w, EventType::TouchUp, {p.x, p.y - 70}, 2); Check(clicked == 0);
        const float old = scroll->GetOffset(); ui.BuildFrame(w, .016); Check(scroll->GetOffset() >= old);
        ui.HandleEvent(Event{EventType::FocusLost}, w); const float stopped = scroll->GetOffset(); ui.BuildFrame(w, .1); Check(scroll->GetOffset() == stopped);
        scroll->SetOffset(0); ui.BuildFrame(w, 0); Event wheel; wheel.type = EventType::MouseWheel; wheel.x = 150; wheel.y = 150; wheel.wheel = -1;
        Check(ui.HandleEvent(wheel, w)); Check(scroll->GetOffset() == 48);
        scroll->SetOffset(1e6f); ui.BuildFrame(w, 0); Check(scroll->GetOffset() == scroll->GetExtent());
        column->Clear(); column->Add(std::make_shared<Text>("Short")); ui.BuildFrame(w, 0); Check(scroll->GetOffset() == 0 && scroll->GetExtent() == 0);
        Reject([&] { scroll->Measure({}); }); Reject([&] { scroll->SetOffset(std::numeric_limits<float>::quiet_NaN()); });
        std::puts("Scroll tap cancellation, pointer IDs, inertia, focus loss, wheel, bounds and shrinking content passed.");
    }
    void SceneTests()
    {
        auto w = View(); Eco::Scene scene; scene.ui.BuildFrame(w, 0);
        Check(scene.pages[0]->GetExtent() > 0 && scene.statistics->GetExtent() > 0);
        const auto statsPoint = Center(scene.statistics); Pointer(scene.ui, w, EventType::TouchDown, statsPoint, 1);
        Pointer(scene.ui, w, EventType::TouchMove, {statsPoint.x - 100, statsPoint.y}, 1);
        Pointer(scene.ui, w, EventType::TouchUp, {statsPoint.x - 100, statsPoint.y}, 1);
        Check(scene.statistics->GetOffset() > 0 && scene.pages[0]->GetOffset() == 0);
        Click(scene.ui, w, scene.navigation[1]); Check(scene.tab == 1);
        const float collapsed = scene.pages[1]->GetExtent(); Click(scene.ui, w, scene.guideHeaders[0]);
        Check(scene.expanded[0] && scene.pages[1]->GetExtent() > collapsed);
        scene.pages[1]->SetOffset(100); scene.ui.BuildFrame(w, 0);
        Click(scene.ui, w, scene.navigation[3]); Check(scene.tab == 3);
        Click(scene.ui, w, scene.profileNameInput); Check(scene.profileNameInput->IsFocused());
        Event edit; edit.type = EventType::KeyDown; edit.key = Key::End; scene.ui.HandleEvent(edit, w);
        edit.type = EventType::TextInput; edit.character = U'!'; scene.ui.HandleEvent(edit, w);
        edit.type = EventType::KeyDown; edit.key = Key::Enter; scene.ui.HandleEvent(edit, w);
        Check(scene.profileNameInput->GetText() == "Эко-Герой!" && !scene.profileNameInput->IsFocused());
        scene.pages[3]->SetOffset(scene.pages[3]->GetExtent()); scene.ui.BuildFrame(w, 0);
        Click(scene.ui, w, scene.darkSwitch); Check(scene.darkMode);
        Click(scene.ui, w, scene.notificationSwitch); Check(!scene.notifications);
        Check(scene.profileNameInput->GetText() == "Эко-Герой!");
        Click(scene.ui, w, scene.navigation[1]); Check(scene.pages[1]->GetOffset() == 100 && scene.expanded[0]);
        scene.ui.HandleEvent(Event{EventType::SurfaceDestroyed}, w); scene.ui.BuildFrame(w, 0); Check(scene.expanded[0]);
        for (auto viewport : {View(320, 480), View(430, 820), View(1080, 2400, 2.625f), View(1920, 1080, 1.25f)})
            for (int i = 0; i < 4; ++i) { scene.SelectTab(i); Check(scene.ui.BuildFrame(viewport, 0).IsReady()); }
        std::puts("EcoSbor four tabs, horizontal/vertical gesture routing, accordion, retained offsets, settings, lifecycle and viewport tests passed.");
    }
    class CaptureContext final : public Beast::Detail::OpenGLContext
    {
        std::unique_ptr<Beast::Detail::OpenGLContext> actual_;
        PFNGLBLITFRAMEBUFFERPROC blit_;
        PFNGLREADPIXELSPROC read_;
        inline static CaptureContext* active_ = nullptr;
        static void APIENTRY Blit(GLint x0, GLint y0, GLint x1, GLint y1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
        {
            auto& s = *active_;
            if (!output.empty())
            {
                std::vector<unsigned char> pixels(static_cast<std::size_t>(x1) * y1 * 4); s.read_(0, 0, x1, y1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                std::size_t colored = 0; for (std::size_t i = 0; i < pixels.size(); i += 4) { if (pixels[i] < 220) ++colored; std::swap(pixels[i], pixels[i + 2]); } Check(colored > 500);
                BITMAPFILEHEADER file{}; file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER); file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
                BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = x1; info.biHeight = y1; info.biPlanes = 1; info.biBitCount = 32;
                std::ofstream out("Test/EcoSbor/Intermediate/Verification/" + output + ".bmp", std::ios::binary);
                out.write(reinterpret_cast<char*>(&file), sizeof(file)); out.write(reinterpret_cast<char*>(&info), sizeof(info)); out.write(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size())); Check(bool(out)); output.clear();
            }
            s.blit_(x0, y0, x1, y1, dx0, dy0, dx1, dy1, mask, filter);
        }
    public:
        inline static std::string output;
        explicit CaptureContext(const Window& w)
        {
            actual_ = Beast::Detail::CreateOpenGLContext(w, {}); blit_ = reinterpret_cast<PFNGLBLITFRAMEBUFFERPROC>(actual_->Load("glBlitFramebuffer"));
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
        RHIBackend backend_; Eco::Scene scene_; std::unique_ptr<RHI> renderer_; int frame_ = 0;
        void Create() { renderer_ = backend_ == RHIBackend::OpenGL ? Beast::Detail::CreateOpenGLRHI(GetWindow(), std::make_unique<CaptureContext>(GetWindow())) : RHI::Create(GetWindow(), {backend_, true, true}); }
    public:
        explicit GpuTest(RHIBackend api) : backend_(api) { scene_.ui.SetTextWarmupEnabled(true); }
        WindowDefinition Configure() const override { WindowDefinition w; w.width = 430; w.height = 820; w.gpuRendering = true; w.title = "EcoSbor verification"; return w; }
        void OnEvent(const Event& e) override
        { scene_.ui.HandleEvent(e, GetWindow()); if (e.type == EventType::SurfaceCreated) Create(); if (e.type == EventType::SurfaceDestroyed) renderer_.reset(); }
        void OnUpdate(double) override
        {
            if (!renderer_) return;
            const auto& w = GetWindow();
            if (frame_ == 0) CaptureContext::output = "windows-home";
            if (frame_ == 3) { scene_.SelectTab(1); CaptureContext::output = "windows-guide"; }
            if (frame_ == 6) { Click(scene_.ui, w, scene_.guideHeaders[0]); CaptureContext::output = "windows-guide-expanded"; }
            if (frame_ == 9) { scene_.SelectTab(2); CaptureContext::output = "windows-map"; }
            if (frame_ == 12) { scene_.SelectTab(3); CaptureContext::output = "windows-profile"; }
            if (frame_ == 15) { scene_.pages[3]->SetOffset(scene_.pages[3]->GetExtent()); CaptureContext::output = "windows-settings"; }
            if (frame_ == 18) { renderer_.reset(); Create(); }
            if (frame_ == 21) Check(SetWindowPos(static_cast<HWND>(w.nativeHandle), nullptr, 0, 0, 820, 620, SWP_NOMOVE | SWP_NOZORDER));
            scene_.ui.Render(*renderer_, w, .016); if (++frame_ == 100) RequestExit();
        }
    };
}
int main()
{
    try {
        TextTests(); ScrollTests(); SceneTests();
        for (auto api : {RHIBackend::OpenGL, RHIBackend::Vulkan}) { GpuTest test(api); Check(Run(test) == 0); }
        std::puts("EcoSbor OpenGL/Vulkan GPU frames, six screen captures (OpenGL), resize and RHI recreation passed."); return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
