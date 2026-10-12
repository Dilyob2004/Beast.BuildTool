#if defined(BEAST_HOT_HOST)
#include "Core/HotReload.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstring>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Beast::HotReload
{
    namespace
    {
        using Entry = const Module* (*)();
        class Host final : public UI::Application
        {
            std::filesystem::path directory_;
            std::string fingerprint_, attempted_;
            UI::AppOptions options_;
            std::unique_ptr<UI::Scene> initial_;
            std::chrono::steady_clock::time_point nextPoll_{};
            // Pin code until process exit: GPU handles/custom deleters and SDK
            // caches may outlive the old scene. Bound generations rather than
            // unloading code with live references. This is not an unbounded leak.
            unsigned loaded_ = 0;
            std::unique_ptr<UI::Scene> Load(const std::filesystem::path& path, bool initial)
            {
                if (loaded_ >= 32) throw std::runtime_error("32 module loads reached; restart beast watch to reclaim pinned code.");
#if defined(_WIN32)
                auto handle = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                if (!handle) throw std::runtime_error("LoadLibrary failed: " + std::to_string(GetLastError()));
                auto entry = reinterpret_cast<Entry>(GetProcAddress(handle, "BeastHotModule"));
#else
                auto handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
                if (!handle) throw std::runtime_error(dlerror());
                auto entry = reinterpret_cast<Entry>(dlsym(handle, "BeastHotModule"));
#endif
                ++loaded_; // Also retain rejected initialized libraries safely.
                if (!entry) throw std::runtime_error("Missing BeastHotModule export. Use BEAST_UI_APPLICATION.");
                const auto* module = entry();
                if (!module || module->version != 1 || module->size != sizeof(Module) || !module->fingerprint ||
                    fingerprint_ != module->fingerprint || !module->create || !module->options)
                    throw std::runtime_error("Incompatible module ABI/SDK: restart beast watch.");
                const auto next = module->options();
                if (initial) options_ = next;
                else if (next.title != options_.title || next.width != options_.width || next.height != options_.height ||
                    next.backend != options_.backend || next.vsync != options_.vsync || next.validation != options_.validation || next.profiling != options_.profiling)
                    throw std::runtime_error("Window/RHI options changed: restart beast watch.");
                std::unique_ptr<UI::Scene> scene(module->create());
                if (!scene) throw std::runtime_error("Module returned an empty scene.");
                return scene;
            }
            void Status(const std::string& generation, const char* result, const std::string& reason = {})
            {
                const auto pending = directory_ / "status.tmp", final = directory_ / "status";
                { std::ofstream file(pending); file << generation << '\n' << result << '\n' << reason << '\n'; }
#if defined(_WIN32)
                MoveFileExW(pending.c_str(), final.c_str(), MOVEFILE_REPLACE_EXISTING);
#else
                std::error_code error; std::filesystem::rename(pending, final, error);
#endif
                Log(("Hot reload " + generation + ": " + result + " " + reason).c_str());
            }
            WindowDefinition ConfigureWindow() const override
            { WindowDefinition w; w.title = options_.title; w.width = options_.width; w.height = options_.height; return w; }
            std::unique_ptr<UI::Scene> CreateScene() override { return std::move(initial_); }
            void BeforeSceneUpdate() override
            {
                const auto now = std::chrono::steady_clock::now();
                if (now < nextPoll_) return;
                nextPoll_ = now + std::chrono::milliseconds(250);
                std::ifstream request(directory_ / "request"); std::string name; std::getline(request, name);
                if (name.empty() || name == attempted_) return;
                attempted_ = name;
                try
                {
                    if (name.size() > 96 || name.find("..") != std::string::npos ||
                        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != std::string::npos)
                        throw std::runtime_error("Invalid module filename.");
                    const auto state = GetScene()->SaveReloadState();
                    if (state.size() > 1024 * 1024) throw std::runtime_error("Reload state exceeds 1 MiB.");
                    auto candidate = Load(directory_ / name, false);
                    if (!candidate->RestoreReloadState(state, GetWindow())) throw std::runtime_error("Candidate rejected the saved state.");
                    candidate->ui.BuildFrame(GetWindow(), 0); // Validate layout/paint before destroying last-good UI.
                    if (!GetScene()->QuiesceForReload()) throw std::runtime_error("Current scene refused to quiesce.");
                    ReplaceScene(std::move(candidate));
                    Status(name, "ok");
                }
                catch (const std::exception& error) { Status(name, "rejected", error.what()); }
                catch (...) { Status(name, "rejected", "Exception initializing candidate."); }
            }
        public:
            // Load before constructing the permanent host so its RHI options
            // originate in the application, not in a generated duplicate.
            struct Initial { UI::AppOptions options; std::unique_ptr<UI::Scene> scene; };
            Host(std::filesystem::path directory, std::string fingerprint, Initial init)
                : UI::Application({init.options.backend, init.options.vsync, init.options.validation, init.options.profiling}),
                  directory_(std::move(directory)), fingerprint_(std::move(fingerprint)), options_(std::move(init.options)), initial_(std::move(init.scene)), loaded_(1)
            { if (!directory_.empty()) { std::filesystem::create_directories(directory_); Status("initial", "ok"); } }
            static Initial Open(const std::filesystem::path& path, const std::string& fingerprint)
            {
                // The temporary loader owns no window/renderer and leaves the
                // module pinned for the final host's lifetime.
                Host loader({}, fingerprint, Initial{});
                loader.loaded_ = 0;
                auto scene = loader.Load(path, true);
                return {std::move(loader.options_), std::move(scene)};
            }
        };
    }
    int RunHost(const char* directory, const char* initialLibrary, const char* fingerprint, void* context)
    {
        try { Host host(directory, fingerprint, Host::Open(initialLibrary, fingerprint)); return Run(host, context); }
        catch (const std::exception& error) { Log(error.what()); return 1; }
    }
}
#endif
