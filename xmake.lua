add_repositories("oxylus https://github.com/oxylusengine/xmake-repo.git")
set_policy("package.precompiled", false)
rule("mode.dist")
on_config(function(target)
  if is_mode("dist") then
    if not target:get("symbols") and target:kind() ~= "shared" then
      target:set("symbols", "hidden")
    end

    if not target:get("optimize") then
      if target:is_plat("android", "iphoneos") then
        target:set("optimize", "smallest")
      else
        target:set("optimize", "fastest")
      end
    end

    if not target:get("strip") then
      target:set("strip", "all")
    end

    target:add("cxflags", "-DNDEBUG")
    target:add("cuflags", "-DNDEBUG")
  end
end)
rule_end()

add_rules("mode.debug", "mode.release", "mode.dist")
add_rules("plugin.compile_commands.autoupdate", { outputdir = "./build", lsp = "clangd" })

set_project("vir")
set_version("1.0.0")

option("tests", { default = false, showmenu = true, description = "Build tests" })
option("profile", { default = false, showmenu = true, description = "Enable Tracy profiling" })

-- GLOBAL COMPILER FLAGS --
set_encodings("utf-8")
add_cxxflags("clang::-fexperimental-library")

-- WARNINGS --
set_warnings("allextra", "pedantic")
add_cxxflags(
  "-Wshadow",
  "-Wno-missing-braces",
  "-Wno-unused-parameter",
  "-Wno-unused-variable",
  { tools = { "clang", "clangxx", "gcc", "cl", "clang_cl", "clang-cl" } }
)
add_cxxflags(
  "-Wshadow-all",
  "-Wno-gnu-line-marker",
  "-Wno-gnu-anonymous-struct",
  "-Wno-gnu-zero-variadic-macro-arguments",
  "-Wno-c2y-extensions",
  { tools = { "clang", "clangxx", "cl", "clang_cl", "clang-cl" } }
)

local fmt_version = "12.1.0"
local fmt_configs = { header_only = false, shared = false }

add_requires("vulkan-headers 1.4.335+0", { system = false })
add_requires("vulkan-memory-allocator v3.1.0", { system = false })
add_requires("fmt " .. fmt_version, { configs = fmt_configs, system = false })
add_requires("unordered_dense v4.8.1")
add_requires("svector v1.0.3")
add_requires("tracy v0.13.1", {
  configs = {
    tracy_enable = has_config("profile"),
    on_demand = true,
    callstack = true,
    callstack_inlines = false,
    code_transfer = true,
    exit = true,
    system_tracing = true,
  },
})

if has_config("tests") then
  add_requires("gtest", {
    debug = is_mode("debug"),
    system = false,
    configs = {
      main = true,
      gmock = true,
    },
  })
end

target("vir")
  set_kind("static")
  set_languages("cxx23")

  add_includedirs("include", { public = true })
  add_headerfiles("include/(vir/*.hpp)")
  add_files("src/*.cpp")

  add_packages("vulkan-headers", "fmt", "unordered_dense", { public = true })
  add_packages("vulkan-memory-allocator", "svector", "tracy")

  if is_plat("linux", "macosx") then
    add_cxxflags("-fPIC")
  end

  if is_mode("debug") then
    add_defines("VIR_DEBUG")
  end
target_end()

if has_config("tests") then
  for _, file in ipairs(os.files("tests/Test*.cpp")) do
    local name = path.basename(file)
    target(name)
      set_kind("binary")
      set_default(false)
      set_languages("cxx23")

      add_deps("vir")
      add_files(file)

      -- only the test targets are instrumented, the vir library they link is not, so a
      -- container annotated on one side of that line and mutated on the other reads as an
      -- overflow. every other ASan check still applies
      add_runenvs("ASAN_OPTIONS", "detect_container_overflow=0")
      add_tests("default", {
        runargs = { "--gmock_verbose=info", "--gtest_stack_trace_depth=10" },
        runenvs = { ASAN_OPTIONS = "detect_container_overflow=0" }
      })

      add_packages("gtest", "svector")

      if is_plat("linux", "macosx") then
        set_policy("build.sanitizer.address", true)
        set_policy("build.sanitizer.undefined", true)
        if is_plat("linux") then
          set_policy("build.sanitizer.leak", true)
        end
      elseif is_plat("windows") then
        if not is_mode("debug") then
          set_policy("build.sanitizer.address", true)
        end
      end

      if is_plat("windows") then
        add_ldflags("/subsystem:console")
      end
    target_end()
  end
end
