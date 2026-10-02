#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Shared by eye buffers and the resolution menu. No graphics/runtime calls here.
struct GevrRenderSize {
    uint32_t baseWidth, baseHeight;
    int32_t width, height;
    double effectiveScale;
    bool limited, fallback;
};

inline GevrRenderSize gevrQuestRenderSize(uint32_t recommendedWidth, uint32_t recommendedHeight,
                                         float requestedScale, uint32_t maxWidth, uint32_t maxHeight) {
    const bool fallback = recommendedWidth < 2 || recommendedHeight < 2;
    const uint32_t baseWidth = (fallback ? 1832u : recommendedWidth) & ~1u;
    const uint32_t baseHeight = (fallback ? 1920u : recommendedHeight) & ~1u;
    maxWidth = std::min(maxWidth, uint32_t(INT32_MAX)) & ~1u;
    maxHeight = std::min(maxHeight, uint32_t(INT32_MAX)) & ~1u;
    if (maxWidth < 2 || maxHeight < 2)
        return {baseWidth, baseHeight, 0, 0, 0, true, fallback};
    const double scale = std::isfinite(requestedScale)
                             ? std::clamp(double(requestedScale), 0.5, 4.0) : 1.0;
    const double effective = std::min({scale, double(maxWidth) / baseWidth, double(maxHeight) / baseHeight});
    const auto evenSize = [](double value) { return std::max(2u, uint32_t(std::lround(value)) & ~1u); };
    return {baseWidth, baseHeight, int32_t(evenSize(baseWidth * effective)),
            int32_t(evenSize(baseHeight * effective)), effective, effective < scale, fallback};
}

// Preserve the desktop port's fixed width and its original rounding exactly.
inline GevrRenderSize gevrLegacyRenderSize(uint32_t recommendedWidth, uint32_t recommendedHeight,
                                          float scale) {
    const bool fallback = recommendedWidth < 2 || recommendedHeight < 2;
    const double aspect = fallback ? 1832.0 / 1920.0 : double(recommendedWidth) / recommendedHeight;
    const uint32_t baseHeight = (uint32_t(std::lround(1832.0 / float(aspect))) + 1u) & ~1u;
    const uint32_t width = uint32_t(1832 * scale);
    return {1832, baseHeight, int32_t(width), int32_t(std::lround(width / float(aspect))),
            scale, false, fallback};
}
