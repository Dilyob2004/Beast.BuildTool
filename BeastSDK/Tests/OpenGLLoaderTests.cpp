#include "RHI/Private/OpenGL/OpenGLFunctions.h"
#include "RHI/Private/OpenGL/OpenGLContext.h"
#include <cstdio>
#include <stdexcept>
#include <string>

using namespace Beast::Detail;
namespace
{
    void Check(bool value) { if (!value) throw std::runtime_error("OpenGL loader test failed"); }
    void StubA() {}
    void StubB() {}
    class FakeContext final : public OpenGLContext
    {
    public:
        int baseLoads = 0, coreLoads = 0;
        std::string missing;
        void* address = reinterpret_cast<void*>(&StubA);
        void* Load(const char* name) override { ++coreLoads; return missing == name ? nullptr : address; }
        void* LoadBase(const char* name) override { ++baseLoads; return missing == name ? nullptr : address; }
        void MakeCurrent() override {}
        bool Present() override { return true; }
        void GetSize(int& width, int& height) override { width = height = 1; }
    };
    void CheckEmpty(const OpenGLFunctions& functions)
    {
#define BEAST_CHECK_EMPTY(Type, Name) Check(functions.Name == nullptr);
        BEAST_GL_ENTRYPOINTS_ALL(BEAST_CHECK_EMPTY)
#undef BEAST_CHECK_EMPTY
    }
    void CheckLoaded(const OpenGLFunctions& functions)
    {
#define BEAST_CHECK_LOADED(Type, Name) Check(functions.Name != nullptr);
        BEAST_GL_ENTRYPOINTS_ALL(BEAST_CHECK_LOADED)
#undef BEAST_CHECK_LOADED
    }
}
int main()
{
    try
    {
        FakeContext context;
        OpenGLFunctions first, second;
        CheckEmpty(first);
        first.Load(context); CheckLoaded(first);
#define BEAST_COUNT_ENTRY(Type, Name) + 1
        constexpr int baseCount = 0 BEAST_GL_ENTRYPOINTS_BASE(BEAST_COUNT_ENTRY);
        constexpr int coreCount = 0 BEAST_GL_ENTRYPOINTS_CORE(BEAST_COUNT_ENTRY);
#undef BEAST_COUNT_ENTRY
        Check(context.baseLoads == baseCount && context.coreLoads == coreCount);
        context.address = reinterpret_cast<void*>(&StubB);
        second.Load(context); CheckLoaded(second);
        Check(first.glClear != second.glClear);
#define BEAST_ENTRY_NAME(Type, Name) #Name,
        for (const char* missing : {BEAST_GL_ENTRYPOINTS_ALL(BEAST_ENTRY_NAME)})
#undef BEAST_ENTRY_NAME
        {
            context.missing = missing;
            bool failed = false;
            try { first.Load(context); }
            catch (const std::runtime_error& error)
            { failed = std::string(error.what()).find(missing) != std::string::npos; }
            Check(failed); CheckEmpty(first); CheckLoaded(second);
            context.missing.clear(); first.Load(context); CheckLoaded(first);
        }
        first.Reset(); CheckEmpty(first); CheckLoaded(second);
        std::puts("OpenGL loader tests passed."); return 0;
    }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
