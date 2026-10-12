#include "Launch/EcoScene.h"
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <stdexcept>
using namespace Beast;
using Clock = std::chrono::steady_clock;
namespace {
    void Check(bool value) { if (!value) throw std::runtime_error("Render benchmark invariant failed."); }
    double Percentile(std::vector<double> v, double p) { std::sort(v.begin(), v.end()); return v[static_cast<std::size_t>((v.size() - 1) * p)]; }
    class Bench final : public Application {
        RHIOptions options_;
        bool eco_;
        bool cache_;
        Eco::Scene scene_;
        UI::Canvas canvas_;
        RHICommandList commands_;
        const RHICommandList* frameCommands_ = nullptr;
        std::unique_ptr<RHI> rhi_;
        Window viewport_;
        int frames_ = 0, attempts_ = 0;
        bool prepared_ = false;
        std::uint64_t lastGpu_ = 0;
        double cpuBuild_ = 0;
        std::vector<double> build_, submit_, gpu_;
        RHILayerStats warm_;
        UI::RepaintCacheStats cacheWarm_;
        void Record() {
            viewport_ = GetWindow(); viewport_.scale = 1;
            const auto start = Clock::now();
            if (eco_) {
                viewport_.scale = 1.25f;
                scene_.pages[0]->SetOffset(20 + (frames_ % 45) * 3.7f);
                frameCommands_ = &scene_.ui.BuildFrame(viewport_, 0);
            } else {
                UI::Theme theme; theme.background = {1, 1, 1, 1}; canvas_.Begin(commands_, viewport_, theme);
                for (int i = 0; i < 8; ++i) {
                    const UI::Rect r{12.f + (i % 2) * 210, 12.f + (i / 2) * 150, 200, 140};
                    canvas_.SaveLayer(r, .75f); canvas_.ClipRRect(r, 18);
                    canvas_.RoundedRect(r, 0, {0, .45f, .2f, 1});
                    canvas_.RoundedRect({r.x + 10, r.y + 40, 80.f + (frames_ % 30), 30}, 10, {1, .6f, .1f, 1});
                    canvas_.Text({r.x + 14, r.y + 10}, "Beast layer", 18, {1, 1, 1, 1});
                    canvas_.Restore();
                }
                canvas_.End();
                frameCommands_ = &commands_;
            }
            cpuBuild_ = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }
    public:
        Bench(RHIBackend backend, bool pool, bool eco, bool cache = false)
            : options_{backend, false, false, true, pool ? 32ull * 1024 * 1024 : 0}, eco_(eco), cache_(cache)
        { scene_.ui.SetRepaintCacheBudget(cache ? 32ull * 1024 * 1024 : 0); }
        WindowDefinition Configure() const override { WindowDefinition w; w.title = "Beast render benchmark"; w.width = 460; w.height = 650; w.gpuRendering = true; return w; }
        void OnEvent(const Event& e) override {
            if (e.type == EventType::SurfaceCreated) rhi_ = RHI::Create(GetWindow(), options_);
            if (e.type == EventType::SurfaceDestroyed) rhi_.reset();
        }
        void OnUpdate(double) override {
            Check(++attempts_ < 5000); if (!rhi_) return;
            if (!prepared_) { Record(); prepared_ = true; }
            if (!rhi_->Submit(viewport_, *frameCommands_)) return;
            const auto s = rhi_->GetFrameStats();
            if (frames_ == 14) { warm_ = s.layers; cacheWarm_ = scene_.ui.GetRepaintCacheStats(); }
            if (frames_ >= 15) { build_.push_back(cpuBuild_); submit_.push_back(s.submitCpuMs);
                if (s.gpuMs && s.gpuFrame >= 16 && s.gpuFrame != lastGpu_) { gpu_.push_back(*s.gpuMs); lastGpu_ = s.gpuFrame; } }
            prepared_ = false;
            if (++frames_ != 75) return;
            Check(s.layers.pooledBytes <= options_.layerPoolBytes && s.layers.pooledTargets <= 16);
            const auto allocations = s.layers.allocations - warm_.allocations, reuses = s.layers.reuses - warm_.reuses;
            if (!eco_) {
                Check(s.layers.renders - warm_.renders == 60 * 16);
                if (options_.layerPoolBytes) Check(allocations == 0 && reuses == 60 * 16);
                else Check(allocations == 60 * 16 && reuses == 0);
            }
            const auto name = options_.backend == RHIBackend::OpenGL ? "OpenGL" : "Vulkan";
            std::printf("BENCH %s %s pool=%s frames=60 build_ms median=%.3f p95=%.3f submit_ms median=%.3f p95=%.3f",
                name, eco_ ? "EcoScroll" : "16Layers", options_.layerPoolBytes ? "on" : "off",
                Percentile(build_, .5), Percentile(build_, .95), Percentile(submit_, .5), Percentile(submit_, .95));
            if (gpu_.empty()) std::printf(" gpu_ms=unavailable");
            else std::printf(" gpu_ms median=%.3f p95=%.3f samples=%zu", Percentile(gpu_, .5), Percentile(gpu_, .95), gpu_.size());
            std::printf(" allocations=%llu reuses=%llu idle_bytes=%llu\n", static_cast<unsigned long long>(allocations),
                static_cast<unsigned long long>(reuses), static_cast<unsigned long long>(s.layers.pooledBytes));
            if (eco_) {
                const auto c = scene_.ui.GetRepaintCacheStats();
                Check(c.cachedBytes <= 32ull * 1024 * 1024 && c.entries <= 64);
                if (cache_) Check(c.hits > cacheWarm_.hits && c.misses == cacheWarm_.misses && allocations == 0 && s.layers.renders == warm_.renders);
                std::printf("  repaint_cache=%s hits=%llu misses=%llu bytes=%llu layer_renders=%llu\n", cache_ ? "on" : "off",
                    static_cast<unsigned long long>(c.hits - cacheWarm_.hits), static_cast<unsigned long long>(c.misses - cacheWarm_.misses),
                    static_cast<unsigned long long>(c.cachedBytes), static_cast<unsigned long long>(s.layers.renders - warm_.renders));
            }
            RequestExit();
        }
        bool Finished() const { return frames_ == 75; }
    };
}
int main() {
    try {
        // Reversed order on the second backend reduces one fixed-order bias.
        for (auto api : {RHIBackend::OpenGL, RHIBackend::Vulkan}) {
            for (bool pool : {false, true}) { Bench b(api, api == RHIBackend::Vulkan ? !pool : pool, false); Check(Run(b) == 0 && b.Finished()); }
            for (bool cache : {false, true}) { Bench eco(api, true, true, api == RHIBackend::Vulkan ? !cache : cache); Check(Run(eco) == 0 && eco.Finished()); }
        }
        std::puts("Times are diagnostics, not test thresholds. GPU queries exclude presentation; CPU Submit includes waits/presentation. Debug build, validation off, vsync off, 15 warm-up frames.");
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
