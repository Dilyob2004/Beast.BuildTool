#include "Launch/EcoScene.h"
#include <chrono>
#include <cstdio>
#include <unordered_map>
using namespace Beast;
namespace {
    using Clock = std::chrono::steady_clock;
    void Check(bool value) { if (!value) throw std::runtime_error("Text atlas benchmark invariant failed."); }
    double P(std::vector<double> v, double p) { std::sort(v.begin(), v.end()); return v[static_cast<std::size_t>((v.size() - 1) * p)]; }
    class Bench final : public Application {
        RHIBackend api_; bool atlas_, eco_; Eco::Scene scene_; UI::Canvas canvas_;
        RHICommandList commands_; const RHICommandList* frame_ = nullptr; Window viewport_;
        std::unique_ptr<RHI> rhi_; int frames_ = 0, attempts_ = 0; bool prepared_ = false;
        std::vector<double> build_, submit_, gpu_; double buildMs_ = 0;
        std::uint64_t gpuFrame_ = 0, draws_ = 0, textureCount_ = 0, textureBytes_ = 0;
        std::unordered_map<const RHITexture*, std::weak_ptr<const RHITexture>> seen_;
        UI::TextCacheStats warm_;
        void Record() {
            viewport_ = GetWindow(); viewport_.scale = 1.25f;
            const auto start = Clock::now();
            if (eco_) { scene_.pages[0]->SetOffset(20 + (frames_ % 45) * 3.7f); frame_ = &scene_.ui.BuildFrame(viewport_, 0); }
            else {
                UI::Theme theme; theme.background = {1,1,1,1}; canvas_.Begin(commands_, viewport_, theme);
                for (int i = 0; i < 24; ++i) canvas_.Text({16, 8.f + i * 20}, "Показатель " + std::to_string(i) + ": " + std::to_string(2000 + frames_ * 23 + i), 16, {.1f,.35f,.2f,1});
                canvas_.End(); frame_ = &commands_;
            }
            buildMs_ = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }
    public:
        Bench(RHIBackend api, bool atlas, bool eco) : api_(api), atlas_(atlas), eco_(eco) {
            canvas_.SetGlyphAtlasBudget(atlas ? 16ull * 1024 * 1024 : 0);
            scene_.ui.SetGlyphAtlasBudget(atlas ? 16ull * 1024 * 1024 : 0);
        }
        WindowDefinition Configure() const override { WindowDefinition w; w.width = 540; w.height = 650; w.title = "Beast text atlas benchmark"; w.gpuRendering = true; return w; }
        void OnEvent(const Event& e) override {
            if (e.type == EventType::SurfaceCreated) rhi_ = RHI::Create(GetWindow(), {api_, false, false, true});
            if (e.type == EventType::SurfaceDestroyed) rhi_.reset();
        }
        void OnUpdate(double) override {
            Check(++attempts_ < 5000); if (!rhi_) return;
            if (!prepared_) { Record(); prepared_ = true; }
            if (!rhi_->Submit(viewport_, *frame_)) return;
            const auto stats = rhi_->GetFrameStats(); const auto text = eco_ ? scene_.ui.GetTextCacheStats() : canvas_.GetTextCacheStats();
            if (frames_ == 59) warm_ = text;
            if (frames_ >= 60) {
                build_.push_back(buildMs_); submit_.push_back(stats.submitCpuMs);
                if (stats.gpuMs && stats.gpuFrame >= 61 && stats.gpuFrame != gpuFrame_) { gpu_.push_back(*stats.gpuMs); gpuFrame_ = stats.gpuFrame; }
            }
            for (const auto& command : frame_->GetCommands()) if (const auto* draw = std::get_if<RHIDrawTriangles>(&command)) {
                if (frames_ >= 60) ++draws_;
                auto& entry = seen_[draw->texture.get()];
                if (entry.expired()) {
                    entry = draw->texture;
                    if (frames_ >= 60 && !draw->texture->GetContent()) { ++textureCount_; const auto& d = draw->texture->GetDesc(); textureBytes_ += std::uint64_t(d.width) * d.height * 4; }
                }
            }
            prepared_ = false;
            if (++frames_ != 180) return;
            Check(text.pageBytes <= 16ull * 1024 * 1024 && text.pages <= 64 && text.runBytes <= 2ull * 1024 * 1024 && text.runs <= 512);
            if (atlas_) Check(text.glyphHits > warm_.glyphHits);
            std::printf("TEXTBENCH %s %s atlas=%s frames=120 build_ms median=%.3f p95=%.3f submit_ms median=%.3f p95=%.3f",
                api_ == RHIBackend::OpenGL ? "OpenGL" : "Vulkan", eco_ ? "EcoScroll" : "24DynamicLabels", atlas_ ? "on" : "off", P(build_, .5), P(build_, .95), P(submit_, .5), P(submit_, .95));
            if (!gpu_.empty()) std::printf(" gpu_ms median=%.3f p95=%.3f", P(gpu_, .5), P(gpu_, .95)); else std::printf(" gpu_ms=unavailable");
            std::printf(" root_draws=%llu new_bitmap_handles=%llu bitmap_bytes=%llu glyph_misses=%llu glyph_hits=%llu page_versions=%llu retained_page_bytes=%llu\n",
                static_cast<unsigned long long>(draws_), static_cast<unsigned long long>(textureCount_), static_cast<unsigned long long>(textureBytes_),
                static_cast<unsigned long long>(text.glyphMisses - warm_.glyphMisses), static_cast<unsigned long long>(text.glyphHits - warm_.glyphHits),
                static_cast<unsigned long long>(text.pagesCreated - warm_.pagesCreated), static_cast<unsigned long long>(text.pageBytes));
            RequestExit();
        }
        bool Finished() const { return frames_ == 180; }
    };
}
int main() {
    try {
        for (auto api : {RHIBackend::OpenGL, RHIBackend::Vulkan}) for (bool eco : {false, true}) for (bool mode : {false, true}) {
            Bench b(api, api == RHIBackend::Vulkan ? !mode : mode, eco); Check(Run(b) == 0 && b.Finished());
        }
        std::puts("Debug; validation/vsync off; DPI 1.25; 60 warm-up + 120 measured frames. Timings are diagnostics, not thresholds. Root texture counters exclude nested layers.");
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
