# Render graph tests

These suites exercise the C++ vir graph. None need a Vulkan device. Pipeline creation and execution checks
use stub Vulkan function pointers and inspect the arguments passed to Vulkan.

## Run

```sh
xmake f --tests=y
xmake build -j 32 -a
xmake test
```

Test targets enable ASan, UBSan and, on Linux, LeakSanitizer. Xmake supplies
`ASAN_OPTIONS=detect_container_overflow=0` because the linked vir library is not instrumented.
If process inspection is restricted, run the tests outside that restriction so LeakSanitizer can finish.

`RenderGraphTest.hpp` supplies declared pipeline bindings and IR inspection helpers. Keep each compiled
`Program` alive while inspecting its instructions. `find_all` rejects temporaries because it returns
pointers into that program.

## Graph coverage

The suites cover the extensions required before integration:

- Subresource aliases, per-mip synchronization and mip generation, including exact mip/layer barriers for
  explicit transfer regions.
- Consecutive image barriers: disjoint mips and layers share a dependency call, compatible neighbours
  coalesce, duplicates disappear and overlapping transitions stay ordered. Execution checks count the
  actual Vulkan calls, including overlapping and disjoint views supplied through different variables.
- Buffer copies, fills and updates; image copies, uploads, readbacks and multisample resolves. Interpreter
  and Recorder operations share range, alignment and buffer-capacity checks, including compressed uploads.
- Mesh/task and ray tracing layouts, draws, pipeline creation and shader binding table construction.
- Separate acceleration structure build, property-query and copy instructions. Build inputs and scratch
  addresses are patched from buffer `ValueID`s at execution, with dependency barriers between builds.
  Runtime build metadata owns its geometry/range arrays; handles and any unpatched addresses remain caller
  supplied. Transient query pools retire with their frame; imported query pools remain caller owned.
- Stencil, depth bias/clamp, tessellation, explicit vertex layouts and instance rates, blend state,
  attachmentless rendering, layer counts and multiview pipeline keys. Image bindings accept a format override.
- Resource names and profiling callbacks; queue-aware timeline tickets; serialized graph execution and
  command pools per thread and queue family. Tests exercise concurrent executions, submission failure and
  retirement through stub Vulkan calls.
- An explicit public `vulkan-headers` dependency and private VMA dependency.

## Ownership during execution

The compiler schedules individual barrier instructions, folds memory dependencies by OR-ing their access
masks and coalesces compatible image ranges. The executor batches consecutive image barriers into Vulkan
dependency calls using stack storage. Any other instruction ends the run, and overlapping resolved ranges
split calls, including different graph roots bound to the same image. Consecutive memory barriers collapse
into one dependency by OR-ing their resolved masks. Program dumps report individual barrier counts.

`RenderGraph::execute` serializes recording, allocation and submission on its queue mutex. Ticket waits run
outside that mutex. Following submissions wait for acceleration-structure query writers to retire before
recording or submitting more work. Register pipelines, bindless sets and profiling callbacks before concurrent execution.
Program variable writes must finish before executing that Program; do not mutate it while an execution reads it.

Frame allocators belong to their frame or upload job. Direct staging allocation, recycling and destruction
must not race execution. Use a separate frame allocator for each background upload job, then wait on its
ticket before retiring its resources. Persistent allocator bookkeeping is locked; callers still control
the lifetime of returned buffers, images, views, samplers and query pools.

Mapped buffer writes must be followed by `PersistentAllocator::flush_buffer` before submission. After waiting
for a GPU-to-host copy, call `invalidate_buffer` before reading mapped bytes. These methods also accept
frame-buffer slices and let VMA handle noncoherent atom alignment. Shader binding table creation flushes
its own mapped writes and aligns region starts without adding padding records to the exposed regions.

Recorder transfer callbacks run inside compute regions and declare every accessed resource on that region.
`Recorder::image` supplies the layout derived from those declarations, including `GENERAL` for mixed reads
and writes. Consecutive callback transfers receive a conservative transfer barrier. Transfers inside a
rendering region are rejected.

## Vulkan integration checks

These are device-free tests. Actual acceleration structure builds, shader binding tables, swapchain resize
and presentation, Tracy integration and visual parity still need integration checks against a real Vulkan device.
