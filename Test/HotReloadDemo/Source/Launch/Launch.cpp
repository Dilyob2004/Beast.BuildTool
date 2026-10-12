#include "Beast.h"

#include "Launch/Version.h"
#include <sstream>
#if defined(DEMO_LINK_ERROR)
void MissingHotReloadSymbol();
#endif
#if defined(DEMO_BAD_ABI)
#undef BEAST_HOT_ABI
#define BEAST_HOT_ABI "intentional-incompatible-test-abi"
#endif

class HotReloadDemoScene final : public Beast::UI::Scene
{
    int ticks_ = 0, clicks_ = 0;
    bool report_ = true;
    std::shared_ptr<Beast::UI::Text> count_;
public:
    HotReloadDemoScene()
    {
        using namespace Beast::UI;
        using namespace Beast::UI::Build;
        if (RejectCandidate) throw std::runtime_error("Test candidate rejected during construction.");
#if defined(DEMO_LINK_ERROR)
        MissingHotReloadSymbol();
#endif
        count_ = Make<Text>("Click: 0", 24);
        ui.SetRoot(Centered(VBox({Make<Text>("Hot reload versions " + std::to_string(DemoVersion), 28), count_,
            Make<Button>("Increment", [this] { count_->SetText("Clicks: " + std::to_string(++clicks_)); })})));
    }
    void OnUpdate(double) override {
        if (report_) { Beast::Log(("HOT_DEMO version=" + std::to_string(DemoVersion) + " restoredTicks=" + std::to_string(ticks_) + " clicks=" + std::to_string(clicks_)).c_str()); report_ = false; }
        ++ticks_;
    }
    std::string SaveReloadState() const override { return "Demo/1 " + std::to_string(ticks_) + " " + std::to_string(clicks_); }
    bool RestoreReloadState(const std::string& state, const Beast::Window&) override {
        std::istringstream in(state); std::string version;
        if (!(in >> version >> ticks_ >> clicks_) || version != "Demo/1" || ticks_ < 0 || clicks_ < 0) return false;
        count_->SetText("Clicks: " + std::to_string(clicks_)); return true;
    }
};

#if defined(BEAST_RHI_VULKAN)
BEAST_UI_APPLICATION(HotReloadDemoScene, {.title = "HotReloadDemo — Beast", .backend = Beast::RHIBackend::Vulkan})
#else
BEAST_UI_APPLICATION(HotReloadDemoScene, {.title = "HotReloadDemo — Beast"})
#endif
