#include "UI.hlsl"

struct DrawConstants { float4 values[8]; };
[[vk::push_constant]] ConstantBuffer<DrawConstants> BeastDraw;

float RoundedDistance(float2 p, float2 size, float radius) {
    float2 q = abs(p - size * .5) - (size * .5 - radius);
    return length(max(q, 0)) + min(max(q.x, q.y), 0) - radius;
}
float Coverage(float distance) {
    return saturate(.5 - distance);
}
float RoundedCoverage(float2 p, float2 size, float radius) {
    if (min(size.x, size.y) <= 0) return 0;
    if (radius <= 0) {
        float2 extent = max(0, min(p + .5, size) - max(p - .5, 0));
        return extent.x * extent.y;
    }
    float distance = RoundedDistance(p, size, radius);
    // Pixel-square bounds, not a wider smoothing kernel. Interior/exterior
    // pixels are exact; integration only runs on the one-pixel contour.
    if (distance <= -.707107) return 1;
    if (distance >= .707107) return 0;
    float2 q = abs(p - size * .5) - (size * .5 - radius);
    // Integrate across the flatter axis to avoid near-vertical circle slopes.
    if (q.x > q.y) { p = p.yx; size = size.yx; }
    float begin = max(0, p.x - .5), end = min(size.x, p.x + .5);
    if (end <= begin) return 0;
    float step = (end - begin) / 8, area = 0;
    [unroll] for (int i = 0; i < 8; ++i) {
        float x = begin + (i + .5) * step;
        float dx = max(max(radius - x, x - (size.x - radius)), 0);
        float root = sqrt(max(0, (radius - dx) * (radius + dx)));
        // Stable form of radius - sqrt(radius^2 - dx^2).
        float cap = dx * dx / max(radius + root, .000001);
        area += max(0, min(p.y + .5, size.y - cap) - max(p.y - .5, cap));
    }
    return saturate(area * step);
}
float ShadowCoverage(float distance, float sigma) {
    // Normal CDF approximation; continuous falloff instead of 12 silhouettes.
    float x = abs(distance) / sigma, t = 1 / (1 + .2316419 * x);
    float tail = .39894228 * exp(-.5 * x * x) * t *
        (.31938153 + t * (-.356563782 + t * (1.781477937 + t * (-1.821255978 + t * 1.330274429))));
    return distance >= 0 ? tail : 1 - tail;
}
float4 LayerMain(FragmentInput input) : SV_Target0 {
    float4 mask = BeastDraw.values[0];
    float4 info = BeastDraw.values[1]; // texture size, radius, group opacity
    float coverage = BeastDraw.values[2].x > 0 ? RoundedCoverage(input.uv * info.xy - mask.xy, mask.zw, info.z) : 1;
    return ColorTexture.Sample(ColorSampler, input.uv) * input.color * (coverage * info.w);
}
float4 ShapeMain(FragmentInput input) : SV_Target0 {
    float4 shape = BeastDraw.values[0]; // physical width/height/radius/border
    float2 p = input.uv;
    if (BeastDraw.values[7].y > 0) {
        float coverage = ShadowCoverage(RoundedDistance(p, shape.xy, shape.z), BeastDraw.values[7].y);
        return BeastDraw.values[1] * coverage * input.color *
            ColorTexture.Sample(ColorSampler, float2(.5, .5)) * BeastDraw.values[7].x;
    }
    float outer = RoundedCoverage(p, shape.xy, shape.z);
    float inner = outer;
    if (shape.w > 0) {
        float2 size = shape.xy - 2 * shape.w;
        inner = min(size.x, size.y) <= 0 ? 0 : min(outer,
            RoundedCoverage(p - shape.w, size, max(0, shape.z - shape.w)));
    }
    float4 endpoints = BeastDraw.values[3];
    float2 direction = endpoints.zw - endpoints.xy;
    float t = saturate(dot(p / shape.xy - endpoints.xy, direction) / dot(direction, direction));
    // Complementary coverage, not a border-colored solid hidden by another
    // fill. Translucent fills and hollow strokes have no colored interior.
    float4 result = lerp(BeastDraw.values[1], BeastDraw.values[2], t) * inner
        + BeastDraw.values[4] * max(0, outer - inner);
    float4 circle = BeastDraw.values[5];
    if (circle.w > 0) {
        float coverage = min(outer, Coverage(length(p - circle.xy) - circle.z));
        float4 ink = BeastDraw.values[6] * coverage;
        result = ink + result * (1 - ink.a);
    }
    // The white texture keeps the existing portable texture/sampler ABI.
    return result * input.color * ColorTexture.Sample(ColorSampler, float2(.5, .5)) * BeastDraw.values[7].x;
}
