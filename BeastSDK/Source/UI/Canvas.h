#pragma once
#include "UI/UI.h"
#include "UI/Private/Font.h"
#include "UI/Private/GlyphAtlas.h"
#include <unordered_map>

namespace Beast::UI
{
    // Painting API for custom Widgets. Coordinates/colors are logical/straight
    // alpha; Canvas handles DPI, clipping and premultiplication for both RHIs.
    class Canvas
    {
        friend class Widget;
        friend class RepaintBoundary;
        RHIPipelineHandle pipeline_;
        RHIPipelineHandle shapePipeline_;
        RHIPipelineHandle layerPipeline_;
        RHITextureHandle white_;
        struct CachedText { Detail::TextBitmap bitmap; std::uint64_t used = 0; };
        std::unordered_map<std::string, CachedText> text_;
        std::uint64_t serial_ = 0;
        std::size_t textBytes_ = 0;
        Detail::GlyphAtlas glyphAtlas_;
        std::vector<Detail::GlyphAtlas::Quad> glyphQuads_;
        std::vector<RHITextureHandle> warmPages_;
        bool discoverWarmup_ = false;
        unsigned nearbyWarmLabels_ = 0;
        struct CachedSubtree
        {
            std::weak_ptr<const Widget> owner;
            std::uint64_t revision = 0, used = 0, bytes = 0;
            std::vector<double> key;
            RHITextureHandle texture;
        };
        std::unordered_map<const Widget*, CachedSubtree> subtrees_;
        RepaintCacheStats repaintStats_;
        std::uint64_t repaintBudget_ = 32ull * 1024 * 1024;
        bool recordingSubtree_ = false;
        std::size_t cacheStateDepth_ = 0;
        struct CacheDepthLimit {};
        void TrimRepaintCache();
        void PaintCached(const RepaintBoundary& widget);
        void CompositeLayer(const RHITextureHandle&, RHIRect pixels, Rect mask, float radius, float opacity, bool rounded, bool effects = false);
        RHICommandList* commands_ = nullptr;
        Window window_;
        Theme theme_;
        // Speculative text region is separate: it never enlarges a paint scissor.
        struct Clip { Rect content, effects, warm; };
        std::vector<Clip> clips_;
        bool paintingShadow_ = false;
        Rect CurrentClip() const { return paintingShadow_ ? clips_.back().effects : clips_.back().content; }
        void PushLayoutClip(Rect rect);
        std::vector<Point> translations_;
        float scale_ = 1;
        bool cacheableTransform_ = true;
        bool snapShapes_ = true;
        unsigned scrollAxes_ = 0; // Nested scrolls can move the same label along either axis.
        Point physicalOrigin_; // Integer crop offset in the root framebuffer's pixel grid.
        struct State { std::vector<Clip> clips; std::vector<Point> translations; float scale; std::size_t layers; bool cacheableTransform; bool snapShapes; unsigned scrollAxes; };
        std::vector<State> states_;
        struct Layer
        {
            std::unique_ptr<RHICommandList> content;
            RHICommandList* parent;
            Window window;
            Point origin;
            std::vector<Clip> clips;
            RHIRect pixels;
            Rect mask;
            float radius, opacity;
            bool rounded;
        };
        std::vector<Layer> layers_;
        Rect Transform(Rect rect) const;
        void BeginLayer(Rect bounds, float opacity, float radius, bool rounded);
        void EndLayer();
        std::vector<RHIVertex> vertices_;
        RHITextureHandle texture_;
        RHIRect scissor_;
        void Flush();
        void BeginBatch(const RHITextureHandle& texture);
        RHIVertex Vertex(Point point, Point uv, RHIColor premultiplied) const;
        RHIVertex PixelVertex(Point point, Point uv, RHIColor premultiplied) const;
        void Shape(Rect rect, float radius, RHIColor from, RHIColor to, Point begin, Point end,
            float borderWidth, RHIColor borderColor, Point inkOrigin = {}, float inkRadius = 0, RHIColor ink = {0, 0, 0, 0}, float shadowSigma = 0);
    public:
        Canvas();
        const Theme& GetTheme() const noexcept { return theme_; }
        float GetPhysicalScale() const noexcept { return scale_ * window_.scale; }
        void SetRepaintCacheBudget(std::uint64_t bytes);
        RepaintCacheStats GetRepaintCacheStats() const { return repaintStats_; }
        void SetGlyphAtlasBudget(std::uint64_t bytes) { glyphAtlas_.SetBudget(bytes); }
        TextCacheStats GetTextCacheStats() const { return glyphAtlas_.GetStats(); }
        bool WarmText() { return glyphAtlas_.WarmStep(warmPages_); }
        void DiscoverWarmText(bool enabled) { discoverWarmup_ = enabled; }
        void Begin(RHICommandList& commands, const Window& window, Theme theme);
        void End();
        // Balanced state scopes. Transformations affect painting, not widget hit testing.
        void Save();
        void Restore();
        void Translate(float x, float y);
        void Scale(float factor); // Positive uniform scale; text rasterizes at the resulting DPI.
        void ClipRect(Rect bounds) { PushClip(bounds); }
        // ClipRRect requires Save/Restore. Its mask is applied once to the group.
        void ClipRRect(Rect bounds, float radius);
        // Saves state and starts a transparent, physical-resolution offscreen group.
        // Restore composites it once. Bounds explicitly clip content AND effects.
        void SaveLayer(Rect bounds, float opacity = 1);
        // Explicit clips constrain both content and decorative overflow.
        void PushClip(Rect rect);
        void PopClip();
        // Select pixel-aligned or fractional painting without changing layout/DPI.
        // Balanced Push/Pop scopes restore clipping, transforms and raster policy.
        // Subpixel mode draws geometry/glyphs directly at native resolution;
        // it does not stretch or bilinearly translate a cached page bitmap.
        void PushScrollOffset(Point offset, bool pixelAligned = true, Axis axis = Axis::Vertical);
        void PopScrollOffset();
        // Edges normally share the hard-clip pixel grid; fractional scroll scopes
        // retain exact geometry. Curves/borders use analytic pixel coverage.
        void RoundedRect(Rect rect, float radius, RHIColor color, float borderWidth = 0, RHIColor borderColor = {0, 0, 0, 0});
        void GradientRect(Rect rect, float radius, RHIColor from, RHIColor to, Point begin = {0, 0}, Point end = {1, 1},
            float borderWidth = 0, RHIColor borderColor = {0, 0, 0, 0});
        // Intersection of the circle and rounded boundary, antialiased once.
        void InkRipple(Rect bounds, float cornerRadius, Point origin, float radius, RHIColor color);
        void Shadow(Rect rect, float radius, float blur, Point offset, RHIColor color);
        void Text(Point origin, const std::string& text, float fontSize, RHIColor color, FontFace face = FontFace::Regular);
    };
}
