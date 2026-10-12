#pragma once
#include <string>
#include <memory>
#include "Core/TextInput.h"
#include "Core/Accessibility.h"

namespace Beast
{
    struct WindowDefinition
    {
        std::string title = "Beast Application";
        int width = 960, height = 640;
        int minWidth = 320, minHeight = 240;
        bool resizable = true;
        bool gpuRendering = false; // Suppress platform diagnostic CPU/GDI drawing.
    };
    // Native handles are borrowed and valid only between SurfaceCreated/Destroyed.
    // They must not be retained across Android backgrounding or rotation.
    struct Window
    {
        void* nativeHandle = nullptr; // HWND or ANativeWindow*
        int width = 0, height = 0;     // physical pixels
        int contentX = 0, contentY = 0, contentWidth = 0, contentHeight = 0;
        float scale = 1.0f;           // pixels per logical unit
        bool focused = false, visible = false;
        double frameTimeSeconds = 0; // Monotonic display/sample clock, same domain as Event::timestamp; 0 disables resampling.
        std::shared_ptr<TextInput> textInput; // Optional platform IME, owned by the platform loop.
        std::shared_ptr<Clipboard> clipboard;
        std::shared_ptr<Accessibility> accessibility;
        int imeBottom = 0; // Physical bottom occlusion, already reflected in contentHeight.
        bool HasSurface() const { return nativeHandle != nullptr; }
    };
}
