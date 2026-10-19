# vir

Vulkan Intermediate Representation: a render graph for modern Vulkan, written in C++23 on top of the
raw Vulkan API and [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator).

Record a frame as a list of passes. `vir` lowers it to an SSA-style instruction list, figures out
which passes depend on which, and inserts the pipeline barriers and layout transitions itself.

> Requires Vulkan 1.3 (dynamic rendering, buffer device address, synchronization2) and a C++23
> compiler with explicit object parameters and `std::expected`. Builds with [xmake](https://xmake.io).

## What it does

- **Render graph.** Passes are recorded into a `Module` as an IR of `ValueID` handles. Each pass
  declares the resources it touches and how (`Access`), and the graph derives the barriers between
  them. Reordering or dropping a pass never means rewriting a barrier by hand.
- **Automatic synchronization.** Read/write/read-write accesses on buffers and images are what the
  graph reads to insert the right barriers, layout transitions, and queue ownership transfers.
- **Compile once, run many.** A `Module` compiles into a `Program`. Inputs that change per frame
  (images, buffers, extents, push data) are declared as variables and filled with `Program::set`
  before each execution, so the graph is not rebuilt every frame.
- **Reflection-driven pipelines.** Vertex layout, push constant ranges, and descriptor bindings come
  from the `ShaderLayout` you hand over with each shader's SPIR-V, so a pipeline is declared from
  little more than its shader blobs. `vir` never parses SPIR-V itself, so produce the layout from your
  shader compiler's reflection, for example Slang's.
- **Dynamic rendering.** No render pass or framebuffer objects. Passes use `begin_rendering`
  directly, with viewport, scissor, blend, and rasterization as pipeline or dynamic state.
- **Compute, transfer and ray tracing.** Compute passes, blits, copies, mip generation, resolves,
  acceleration structure builds and `trace_rays` live in the same graph as graphics, so
  cross-domain dependencies are resolved the same way.
- **Allocators.** A `PersistentAllocator` for long-lived resources and a `FrameAllocator` for
  transients, both over VMA, with buffer device address enabled.

## A recorded pass

```cpp
const auto cleared = module.clear(swapchain_image, vir::ClearValue::rgba_f32(0.1f, 0.1f, 0.1f, 1.0f));

const auto [target] = module.begin_rendering({{cleared, vir::Access::ColorRW}})
                        .with_name("triangle")
                        .bind_graphics_pipeline(pipeline)
                        .set_viewport(0, vir::Rect2D::framebuffer())
                        .set_scissor(0, vir::Rect2D::framebuffer())
                        .broadcast_color_blend(vir::BlendPreset::Off)
                        .push_constants(push)
                        .draw(3, 1)
                        .end_rendering();
```

Every scalar descriptor type in a shader's layout is ordinary pass state: samplers, sampled and
storage images, input attachments, uniform and storage buffers, texel buffers, and acceleration
structures. Binding methods describe the payload. The pipeline that is active at each consuming draw
or dispatch supplies the exact Vulkan descriptor type and the statically reachable access. For
example, `bind_image` covers sampled images, storage images, and input attachments, and
`bind_buffer` covers both uniform and storage buffers:

```cpp
const auto [target] = module.begin_rendering({{target, vir::Access::ColorRW}})
                        .bind_graphics_pipeline(pipeline)
                        .bind_texture(0, 3, texture, sampler)
                        .bind_buffer(0, 4, material)
                        .draw(3, 1)
                        .end_rendering();
```

Closing a rendering or compute pass returns one fresh `ValueID` for every resource declared at the
beginning of the pass, in the same order. The count is deduced from the braced list or `std::array`
given to `begin_rendering` or `begin_compute`, and the result is a `std::array` for structured
bindings. A runtime-sized `std::span` returns the module instead, so close it with
`end_rendering<N>()`, `end_compute<N>()`, or `end_rendering_values()` and `end_compute_values()`.
Read-only resources also receive an after-pass value, so using that value explicitly orders later
work after the pass. Passes must declare at least one resource.

`bind_buffer_range` binds an explicit byte range, and `bind_texel_buffer` similarly covers both
uniform and storage texel buffers. Sampler-only, combined image/sampler, texel-buffer, and
acceleration-structure bindings remain separate because their Vulkan payloads differ. A descriptor
write may precede the pipeline bind, but a pipeline must be active when a draw or dispatch consumes
it. If the same standing write is consumed by multiple pipelines, they must agree on its descriptor
type. Rebind between pipelines when they do not.

Texel-buffer views and acceleration structures remain caller-owned. Their binding calls also take the
backing buffer value so the graph can synchronize it. The caller must keep raw sampler, buffer-view,
and acceleration-structure handles alive through execution.

Bindless is opt-in per pipeline. Pass the caller-owned layout and set at the index it should
occupy. `vir` splices that layout into the pipeline layout and binds the set without updating or
destroying it. The caller is also responsible for enabling any descriptor-indexing features required
by that layout and its shaders:

```cpp
const auto pipeline = graph.declare_graphics_pipeline({
  .shaders = {vertex_shader, fragment_shader},
  .bindless = vir::BindlessDescriptorSet{.index = 2, .layout = bindless_layout, .set = bindless_set},
  .name = "mesh",
});
```

Images reached through an external bindless set still need an entry in `begin_rendering` or
`begin_compute` when the graph must synchronize them, because their descriptor contents are
intentionally opaque to the IR. One resource-array value can stand for the whole set. The access is
applied to every element while the array remains a single attachment entry:

```cpp
const auto [target, images] = module.begin_rendering({
                                  {target, vir::Access::ColorRW},
                                  {bindless_images, vir::Access::FragmentRead},
                                })
                                // ...
                                .end_rendering();
```

A compute pass declares its accesses, and the graph barriers the dispatches against each other and
against any draw that later reads what they wrote:

```cpp
const auto [placed] = module.begin_compute({{instances, vir::Access::ComputeWrite}})
                        .bind_compute_pipeline(place)
                        .push_constants(push)
                        .dispatch_invocations(triangle_count, 1, 1)
                        .end_compute();
```

## Variables and programs

Anything that changes between executions is a variable. Declare it while recording, compile once,
then fill it before each run:

```cpp
auto module = vir::Module{};
const auto color = module.declare_image_var("color", format, vir::Access::FragmentSampled);
const auto extent = module.declare_extent_2d_var("extent", {1280, 720});
// ... record passes that use `color` and `extent`, ending in `output`

auto program = module.compile(graph.pipeline_bindings(), output);
if (!program) {
  return std::unexpected(program.error());
}

program->set(color, color_attachment);
program->set(extent, vir::Extent2D{width, height});
const auto ticket = graph.execute(*program, frame_allocator);
```

Setting a variable the compiled program does not contain is a no-op. Use `has_variable` to skip
work for inputs that were optimized out. A renderer usually caches one `Program` per frame shape and
only calls `set` per frame.

## Setup

You supply the instance, physical device, and logical device. `vir` loads its own function table:

```cpp
auto context = vir::Context::create({
  .instance = instance,
  .physical_device = physical_device,
  .device = device,
  .get_instance_proc_addr = vkGetInstanceProcAddr,
});
(*context)->create_command_queue(graphics_family, vir::DomainFlag::Graphics);

auto persistent = vir::PersistentAllocator(**context);
auto graph = vir::RenderGraph(**context, persistent);
auto frame_allocator = vir::FrameAllocator(persistent);
```

Logs and failed assertions go through a sink, which prints to stderr by default. Route them into
your own logger with `vir::set_log_sink`.

## Building and tests

```sh
xmake f -m debug --tests=y
xmake b -a
xmake test
```

The tests are GoogleTest binaries under `tests/`, built with ASan, UBSan and (on Linux) LSan. Most
need no Vulkan device; pipeline creation and execution checks use stub Vulkan function pointers and
inspect the arguments passed to Vulkan. See `tests/README.md`.

## Using it in your own project

`vir` is consumed as an xmake package. Its dependencies are `vulkan-headers`, `fmt` and
`unordered_dense` (public), plus `vulkan-memory-allocator`, `svector` and `tracy` (private). Pin
them to the versions your project already uses with `add_requireconfs("vir.<dep>", ...)` so both
resolve to one instance.

Then include what you need:

```cpp
#include <vir/Module.hpp>
#include <vir/RenderGraph.hpp>
```

## License

MIT. See [LICENSE](LICENSE).

This project is heavily inspired by the [vuk](https://github.com/martty/vuk). Please check it out.
