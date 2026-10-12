#include "Beast.h"

class {{PROJECT_NAME}}Scene final : public Beast::UI::Scene
{
public:
    {{PROJECT_NAME}}Scene()
    {
        using namespace Beast::UI;
        using namespace Beast::UI::Build;
        ui.SetRoot(Centered(Make<Text>("Hello, {{PROJECT_NAME}}!", 24.0f)));
    }
};

#if defined(BEAST_RHI_VULKAN)
BEAST_UI_APPLICATION({{PROJECT_NAME}}Scene, {.title = "{{PROJECT_NAME}} — Beast", .backend = Beast::RHIBackend::Vulkan})
#else
BEAST_UI_APPLICATION({{PROJECT_NAME}}Scene, {.title = "{{PROJECT_NAME}} — Beast"})
#endif
