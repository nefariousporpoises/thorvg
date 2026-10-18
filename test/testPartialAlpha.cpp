/* Copyright (c) 2026 ThorVG project. MIT license; see LICENSE. */
#include <thorvg.h>
#include <memory>
#include <algorithm>
#include <vector>
#include "config.h"
#include "catch.hpp"

using namespace tvg;

#ifdef THORVG_CPU_ENGINE_SUPPORT
#ifdef THORVG_PARTIAL_RENDER_SUPPORT
namespace
{
constexpr uint32_t W = 400, H = 200, Stride = 416;
constexpr uint32_t Padding = 0x12345678;

Scene* alphaScene(uint8_t value, Shape** moving = nullptr)
{
    auto root = Scene::gen();
    auto group = Scene::gen();
    for (float x : {30.0f, 150.0f}) {
        auto shape = Shape::gen();
        REQUIRE(shape->appendRect(x, 30, 100, 140) == Result::Success);
        REQUIRE(shape->fill(120, 40, 200) == Result::Success);
        REQUIRE(group->add(shape) == Result::Success);
    }
    REQUIRE(group->opacity(128) == Result::Success);
    REQUIRE(root->add(group) == Result::Success);
    auto mover = Shape::gen();
    REQUIRE(mover->appendRect(330, 30, 40, 40) == Result::Success);
    REQUIRE(mover->fill(value, 200, 40, 128) == Result::Success);
    REQUIRE(root->add(mover) == Result::Success);
    if (moving) *moving = mover;
    return root;
}

bool contains(const Region& r, uint32_t x, uint32_t y)
{
    return int32_t(x) >= r.xMin && int32_t(x) < r.xMax && int32_t(y) >= r.yMin && int32_t(y) < r.yMax;
}
}  // namespace

TEST_CASE("Partial alpha stays equal to a fresh render", "[partial-alpha]")
{
    auto cs = GENERATE(ColorSpace::ABGR8888S, ColorSpace::ARGB8888S, ColorSpace::ABGR8888, ColorSpace::ARGB8888);
    auto threads = GENERATE(0u, 2u);
    auto smart = GENERATE(true, false);
    REQUIRE(Initializer::init(threads) == Result::Success);
    {
        std::vector<uint32_t> pixels(Stride * H, Padding);
        for (uint32_t y = 0; y < H; ++y)
            std::fill_n(pixels.data() + y * Stride, W, 0);
        auto canvas = std::unique_ptr<SwCanvas>(SwCanvas::gen(smart ? EngineOption::SmartRender : EngineOption::None));
        REQUIRE(canvas->target(pixels.data(), Stride, W, H, cs) == Result::Success);
        Shape* mover = nullptr;
        REQUIRE(canvas->add(alphaScene(20, &mover)) == Result::Success);
        Region dummy{};
        REQUIRE(canvas->dirtyRegions() == 0);
        REQUIRE(canvas->dirtyRegions(&dummy, 1) == Result::InsufficientCondition);
        for (uint32_t frame = 0; frame < 6; ++frame) {
            auto before = pixels;
            if (frame) REQUIRE(mover->fill(uint8_t(20 + frame), 200, 40, 128) == Result::Success);
            REQUIRE(canvas->update() == Result::Success);
            REQUIRE(canvas->dirtyRegions(&dummy, 1) == Result::InsufficientCondition);
            // Without SmartRender the caller requests a clear on every draw.
            // Final smart frame exercises the explicit full-render fallback.
            REQUIRE(canvas->draw(!smart || frame == 5) == Result::Success);
            auto count = canvas->dirtyRegions();
            REQUIRE(count > 0);
            std::vector<Region> regions(count);
            REQUIRE(canvas->dirtyRegions(regions.data(), count) == Result::Success);
            REQUIRE(canvas->dirtyRegions(nullptr, count) == Result::InvalidArguments);
            REQUIRE(canvas->dirtyRegions(regions.data(), 0) == Result::InvalidArguments);
            uint64_t area = 0;
            for (const auto& r : regions) {
                REQUIRE(r.xMin >= 0);
                REQUIRE(r.yMin >= 0);
                REQUIRE(r.xMax <= int32_t(W));
                REQUIRE(r.yMax <= int32_t(H));
                REQUIRE(r.xMax > r.xMin);
                REQUIRE(r.yMax > r.yMin);
                area += uint64_t(r.xMax - r.xMin) * uint64_t(r.yMax - r.yMin);
            }
            if (smart && frame > 0 && frame < 5) REQUIRE(area < W * H);
            if (!smart || frame == 5) REQUIRE(area == W * H);
            REQUIRE(canvas->sync() == Result::Success);
            REQUIRE(canvas->dirtyRegions() == 0);
            REQUIRE(canvas->dirtyRegions(&dummy, 1) == Result::InsufficientCondition);
            std::vector<uint32_t> fresh(Stride * H, Padding);
            for (uint32_t y = 0; y < H; ++y)
                std::fill_n(fresh.data() + y * Stride, W, 0);
            auto full = std::unique_ptr<SwCanvas>(SwCanvas::gen(smart ? EngineOption::SmartRender : EngineOption::None));
            REQUIRE(full->target(fresh.data(), Stride, W, H, cs) == Result::Success);
            REQUIRE(full->add(alphaScene(uint8_t(20 + frame))) == Result::Success);
            REQUIRE(full->draw(true) == Result::Success);
            REQUIRE(full->sync() == Result::Success);
            CAPTURE(frame, int(cs), threads, smart);
            const bool equal = pixels == fresh;
            REQUIRE(equal);
            for (uint32_t y = 0; y < H; ++y) {
                for (uint32_t x = 0; x < Stride; ++x) {
                    if (x >= W) {
                        REQUIRE(pixels[y * Stride + x] == Padding);
                        continue;
                    }
                    if (pixels[y * Stride + x] == before[y * Stride + x]) continue;
                    bool covered = false;
                    for (const auto& r : regions)
                        covered |= contains(r, x, y);
                    REQUIRE(covered);
                }
            }
        }
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Pixel alpha formats have the advertised channel values", "[partial-alpha]")
{
    auto cs = GENERATE(ColorSpace::ABGR8888S, ColorSpace::ARGB8888S, ColorSpace::ABGR8888, ColorSpace::ARGB8888);
    REQUIRE(Initializer::init() == Result::Success);
    {
        std::vector<uint32_t> pixels(16 * 16);
        auto canvas = std::unique_ptr<SwCanvas>(SwCanvas::gen(EngineOption::SmartRender));
        REQUIRE(canvas->target(pixels.data(), 16, 16, 16, cs) == Result::Success);
        auto shape = Shape::gen();
        REQUIRE(shape->appendRect(0, 0, 16, 16) == Result::Success);
        REQUIRE(shape->fill(200, 100, 50, 128) == Result::Success);
        REQUIRE(canvas->add(shape) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        const auto pixel = pixels[8 * 16 + 8];
        const bool abgr = cs == ColorSpace::ABGR8888 || cs == ColorSpace::ABGR8888S;
        const bool straight = cs == ColorSpace::ABGR8888S || cs == ColorSpace::ARGB8888S;
        REQUIRE((pixel >> 24) == 128);
        auto red = (pixel >> (abgr ? 0 : 16)) & 255;
        auto green = (pixel >> 8) & 255;
        auto blue = (pixel >> (abgr ? 16 : 0)) & 255;
        // Integer premultiply/unpremultiply may lose one unit.
        REQUIRE(red == Approx(straight ? 200 : 100).margin(1));
        REQUIRE(green == Approx(straight ? 100 : 50).margin(1));
        REQUIRE(blue == Approx(straight ? 50 : 25).margin(1));
    }
    REQUIRE(Initializer::term() == Result::Success);
}
#endif
#endif
