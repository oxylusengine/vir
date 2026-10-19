#include "RenderGraphTest.hpp"
#include "vir/RenderGraph.hpp"

namespace vir::tests {
template <typename T>
concept can_end_rendering = requires(T pass) { pass.end_rendering(); };

template <typename T>
concept can_end_compute = requires(T pass) { pass.end_compute(); };

static auto module_with_image() -> std::pair<Module, ValueID> {
  auto module = Module{};
  const auto image = module.transient_image(untyped_info());
  return {std::move(module), image};
}

TEST(PassHandles, ReturnTypesCarryTheDeducedCount) {
  auto module = Module{};
  const auto a = ResourceAccess{};
  const ResourceAccess list[2] = {a, a};
  const auto arr = std::array{a, a, a};
  const auto runtime = std::vector{a};

  static_assert(std::is_same_v<decltype(module.begin_rendering({{}, {}})), RenderingPass<2>>);
  static_assert(std::is_same_v<decltype(module.begin_rendering(list)), RenderingPass<2>>);
  static_assert(std::is_same_v<decltype(module.begin_rendering(arr)), RenderingPass<3>>);
  static_assert(std::is_same_v<decltype(module.begin_rendering_area(arr, Extent2D{1, 1})), RenderingPass<3>>);
  static_assert(std::is_same_v<decltype(module.begin_compute({{}})), ComputePass<1>>);
  static_assert(std::is_same_v<decltype(module.begin_compute(arr)), ComputePass<3>>);
  static_assert(std::is_same_v<decltype(module.begin_compute(std::span<const ResourceAccess>(runtime))), Module&>);
  static_assert(std::is_same_v<decltype(module.begin_compute(runtime)), Module&>);
  static_assert(std::is_same_v<decltype(module.begin_rendering(runtime)), Module&>);
  static_assert(std::is_same_v<decltype(std::declval<RenderingPass<2>>().end_rendering()), std::array<ValueID, 2>>);
  static_assert(std::is_same_v<decltype(std::declval<ComputePass<4>>().end_compute()), std::array<ValueID, 4>>);
  static_assert(std::is_same_v<decltype(std::declval<ComputePass<1>>().dispatch(1_u32, 1_u32, 1_u32)), ComputePass<1>>);
  static_assert(std::is_same_v<decltype(std::declval<RenderingPass<1>>().draw(1_u32, 1_u32)), RenderingPass<1>>);

  static_assert(can_end_compute<ComputePass<1>>);
  static_assert(!can_end_rendering<ComputePass<1>>);
  static_assert(can_end_rendering<RenderingPass<1>>);
  static_assert(!can_end_compute<RenderingPass<1>>);
}

TEST(PassHandles, BracedDeductionMatchesTheSpanPath) {
  auto [deduced, image] = module_with_image();
  const auto [rendered, aux] = deduced.begin_rendering({{image, Access::ColorRW}, {image, Access::FragmentRead}})
                                 .bind_graphics_pipeline(pipeline(0))
                                 .set_viewport(0, Rect2D::framebuffer())
                                 .draw(3_u32, 1_u32)
                                 .end_rendering();
  EXPECT_NE(rendered, aux);
  const auto [written] = deduced.begin_compute({{rendered, Access::ComputeWrite}})
                           .bind_compute_pipeline(pipeline(1))
                           .dispatch(1_u32, 1_u32, 1_u32)
                           .end_compute();

  auto [spanned, image2] = module_with_image();
  const std::array accesses{ResourceAccess{image2, Access::ColorRW}, ResourceAccess{image2, Access::FragmentRead}};
  spanned.begin_rendering(std::span<const ResourceAccess>(accesses))
    .bind_graphics_pipeline(pipeline(0))
    .set_viewport(0, Rect2D::framebuffer())
    .draw(3_u32, 1_u32);
  const auto span_values = spanned.end_rendering<2>();
  const auto compute_accesses = std::array{ResourceAccess{span_values[0], Access::ComputeWrite}};
  spanned.begin_compute(std::span<const ResourceAccess>(compute_accesses))
    .bind_compute_pipeline(pipeline(1))
    .dispatch(1_u32, 1_u32, 1_u32);
  const auto span_written = spanned.end_compute<1>()[0];

  EXPECT_EQ(compile(deduced, written).dump(), compile(spanned, span_written).dump());
}

TEST(PassHandles, StdArrayDeduces) {
  auto [module, image] = module_with_image();
  const auto accesses = std::array{ResourceAccess{image, Access::ComputeWrite}};
  const auto [written] = module.begin_compute(accesses)
                           .bind_compute_pipeline(pipeline(0))
                           .dispatch(1_u32, 1_u32, 1_u32)
                           .end_compute();
  const auto compiled = compile(module, written);
  EXPECT_EQ(find_all<ir::Dispatch>(compiled).size(), 1u);
}

TEST(PassHandles, ASplitPassLoopsDispatchesBeforeEnding) {
  auto [module, image] = module_with_image();
  auto pass = module.begin_compute({{image, Access::ComputeWrite}});
  pass.bind_compute_pipeline(pipeline(0));
  for (u32 index = 0; index < 3; index++) {
    pass.dispatch(index + 1, 1_u32, 1_u32);
  }

  const auto [written] = pass.end_compute();
  const auto compiled = compile(module, written);
  EXPECT_EQ(find_all<ir::Dispatch>(compiled).size(), 3u);
}

TEST(PassHandles, ConditionArmsReturnHandleResults) {
  auto [module, image] = module_with_image();
  const auto enabled = module.declare_bool_var("enabled", true);
  const auto merged = module.set_condition(
    enabled,
    [&](Module& m) {
      return m.begin_rendering({{image, Access::ColorRW}})
        .bind_graphics_pipeline(pipeline(0))
        .draw(3_u32, 1_u32)
        .end_rendering();
    },
    [&](Module&) { return std::array{image}; }
  );
  EXPECT_EQ(module.resource_root(merged[0]), image);
}
} // namespace vir::tests
