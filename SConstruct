#!/usr/bin/env python
"""stubby-rack: the Godot-native modular-synth engine for wait-for-stubs.

    scons                      build the extension (template_debug) against api/extension_api.json
    scons target=template_release
    STUBBY_API=4.7 scons       build against godot-cpp's bundled 4.7 API instead (CI baseline)
    scons tests                build and run the core tests (no Godot needed)
"""
import os

env = Environment(tools=["default"], PLATFORM="")
api = os.environ.get("STUBBY_API", "custom")
if api == "custom":
    # Dumped from the installed Godot 4.8 binary: `godot --headless --dump-extension-api`.
    env["custom_api_file"] = File("api/extension_api.json").abspath
else:
    env["api_version"] = api
env = SConscript("godot-cpp/SConstruct", {"env": env})

# The DSP core is plain C++17 and never includes godot-cpp.
CORE_INCLUDES = [
    "src/",
    "thirdparty/choc",
    "thirdparty/signalsmith",
    "thirdparty/daisysp/Source",
    "thirdparty/mutable",
]
env.Append(CPPPATH=CORE_INCLUDES)

core_sources = Glob("src/core/*.cpp")
sources = core_sources + Glob("src/godot/*.cpp")

library = env.SharedLibrary(
    "demo/addons/stubby_rack/bin/libstubby_rack{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
    source=sources,
)
Default(library)

# Core tests: a separate environment with no godot-cpp, so the core can't depend on it.
test_env = Environment(CXXFLAGS=["-std=c++17", "-O2", "-Wall", "-Wextra"], CPPPATH=CORE_INCLUDES + ["thirdparty/doctest"])
test_objs = [test_env.Object("build/tests/" + os.path.splitext(os.path.basename(str(s)))[0], s) for s in Glob("tests/*.cpp")]
core_objs = [test_env.Object("build/core/" + os.path.splitext(os.path.basename(str(s)))[0], s) for s in core_sources]
test_bin = test_env.Program("build/tests/stubby_rack_tests", test_objs + core_objs)
run_tests = test_env.Command("build/tests/.passed", test_bin, "$SOURCE && touch $TARGET")
AlwaysBuild(run_tests)
Alias("tests", run_tests)
