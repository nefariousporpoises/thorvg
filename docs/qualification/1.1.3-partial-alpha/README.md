# ThorVG 1.1.3 partial-alpha port qualification

## Provenance and scope

- Release: v1.1.3, `589bc4120f2097fb1b135e552c2ef39f6581612b`.
- Behaviour ported from [upstream PR #4807](https://github.com/thorvg/thorvg/pull/4807), head `3315716210ba16337082908a66432710036ae6fd`, author Stephen-Psaradellis. This is a port, not a clean cherry-pick or claim of upstream acceptance.
- Production/test implementation: `96f4d2c5eb393969a80379632c26693dd9f35784`.
- Existing public dirty-region API (#4919) is part of 1.1.3. No extra #4919 patch or #4872 allocator change is included.
- Modular's renderer pin and working files were not changed. This is library-level qualification, not #509/#510 acceptance.

The port converts only committed dirty partitions from premultiplied to straight alpha on a partial draw. Full draws or disabled partial rendering retain whole-surface conversion. It uses 1.1.3's existing SIMD-aware row conversion, rather than replacing it with the older PR's scalar pixel loop. Dirty partitions remain available until sync.

## Verified contracts

`test/testPartialAlpha.cpp` exercises the public C++ SwCanvas boundary:

- ABGR8888 / ARGB8888 are premultiplied; ABGR8888S / ARGB8888S are straight alpha. An independent solid-colour fixture checks alpha and channel values with one-unit integer-rounding tolerance.
- Six successive frames compare partial output against fresh full renders, pixel-exactly, with unchanged translucent composited rectangles and a changing translucent shape.
- Four formats, 0/2 worker threads, SmartRender and non-smart full rendering are exercised. SmartRender uses draw(false), then an explicit draw(true) fallback. Without SmartRender, the caller uses draw(true); draw(false) is not a promise to clear an existing target.
- Query regions after draw and before sync; queries before draw/after sync refuse with InsufficientCondition and count zero. Null and zero-capacity retrieval are rejected.
- Regions stay inside the target and cover every changed pixel. Partial updates are smaller than the target; explicit full updates report full coverage. Copy region metadata before sync and read/present pixels only after sync.
- A 416-pixel stride for a 400-pixel image preserves sentinel padding.

## Reproduction and results

macOS 26.6.1, arm64; Apple Clang 21.0.0; Meson 1.11.2; Ninja 1.13.2. CPU renderer, partial=true, SVG/TTF loaders, static library, debug build (upstream optimization defaults retained), OpenMP disabled. Changed lines formatted with clang-format 18.1.8. No performance benchmark was run.

```
meson setup build-port -Dtests=true -Dloaders=svg,ttf -Dextra= -Ddefault_library=static -Dbuildtype=debug
ninja -C build-port -j4
build-port/test/tvgUnitTests

meson setup build-asan -Dtests=true -Dloaders=svg,ttf -Dextra= -Ddefault_library=static -Dbuildtype=debug -Db_sanitize=address,undefined -Db_lundef=false
ninja -C build-asan -j4
build-asan/test/tvgUnitTests

meson setup build-scalar -Dtests=true -Dloaders=svg,ttf -Dextra= -Ddefault_library=static -Dbuildtype=debug -Dsimd=false
ninja -C build-scalar -j4
build-scalar/test/tvgUnitTests '[tvgSwEngine],[tvgSwCanvas],[partial-alpha]'
```

| Evidence | Result |
| --- | --- |
| Restore unpatched v1.1.3 postRender, retain tests/helper, rebuild | Fails equality at frame 1, ABGR8888S, zero workers. Exit SIGABRT is Catch's exception-disabled assertion-abort path, not an OOM finding. See red.log. |
| Fixed configured suite, NEON | 70/70 cases; 875,067 assertions, suite.log |
| Fixed configured suite, ASan + UBSan, NEON | 70/70 cases; 875,067 assertions; no sanitizer diagnostics, asan-ubsan.log |
| Fixed focused CPU/canvas/contract suite, scalar | 18/18 cases; 874,238 assertions, scalar.log |
| Independent source reviews | Standards: no actionable findings. Spec/correctness: no blocking findings. Reviewers did not rerun tests. |

The initial fixture wrongly seeded active pixels with padding sentinels; that failed at frame zero and was corrected. It is not counted as the regression proof. The retained red log is the corrected fixture failing at frame one with old postRender restored. The old renderer was removed again before fixed verification.

## Modular integration boundary

Read-only inspection of Modular `3a69d7fced14dd99138dc80070fea60f148cea60` found `gui/src/thorvg_adapter.cpp` configureTarget uses ABGR8888S, while `adapters/clap/src/gui_surface_mac.mm` declares premultiplied RGBA to CoreGraphics. Opaque output masks that representational mismatch. This port fixes repeated unpremultiplication, not that adapter contract.

Before allowing translucent editor output, use an explicit premultiplied editor target (ABGR8888 on little-endian arm64 gives RGBA bytes) or perform an explicit format conversion at the boundary. Preserve the straight-alpha export contract separately. Native presentation and partial OS blitting were not changed or executed here.

## Remaining qualification / non-claims

- The new regression fixture changes colour only. It does not itself qualify movement/removal, resize/retargeting, no-change frames, effects that deactivate partial rendering, or native presentation. Existing upstream canvas tests include movement and were run.
- No C API wrapper execution, GL/WebGPU, Windows/Linux, OpenMP, x86 AVX2, all-loader/all-saver matrix, TSan, or clang-tidy claim. SIMD scalar and arm64 NEON are tested.
- This does not fix ThorVG noexcept OOM termination. No allocator mechanism, allocation-sweep ordinals, or Modular memory budgets were qualified.
- Before Modular adopts this pin, rerun its Exact/presented-pixel, structural/lifecycle, recovery and resource-accounting checks. Prior 1.1.1 evidence cannot qualify this dependency version.
