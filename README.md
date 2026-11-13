# stubby-rack

The Godot-native modular-synth engine for
[wait-for-stubs](https://github.com/buddha314/wait-for-stubs): VCV Rack semantics
(every module once per sample, 1-sample cables, feedback allowed, VCV voltages)
inside Godot's own audio mix, as a C++ GDExtension built on godot-cpp. It also owns
the game's **master clock**: one sample counter and transport that time every sound in
the game.

**Status:** scaffold. The extension loads in Godot 4.8 and exposes `RackServer`; the
core has VCV voltage helpers and their tests. The engine itself is specified, not yet
built.

## Specification

The requirements and design live in the game repository, in OpenSpec:
`wait-for-stubs/openspec/changes/add-rack-audio-engine/` (proposal, design D1–D10,
specs for audio-engine, master-clock, modular-rack, rack-control, spatial-emission,
synth-modules, and tasks). Implement against those, and change the spec there when
reality disagrees.

## Layout

```
src/core/      the DSP engine: plain C++17, never includes godot-cpp, testable without Godot
src/godot/     Godot-facing classes: RackServer singleton, (later) RackPatch, Rack, RackEmitter
tests/         doctest tests of src/core
thirdparty/    vendored, permissively licensed DSP and utilities (see THIRDPARTY.md)
godot-cpp/     submodule, pinned to 10.0.0-stable
api/           extension_api.json dumped from the Godot 4.8 binary we build against
demo/          a minimal Godot project; the build writes the addon to demo/addons/stubby_rack/
tools/         check_licences.py
```

## Build

Needs SCons 4, a C++17 compiler, Python 3 and the Godot 4.8 binary.

```bash
git clone --recursive https://github.com/buddha314/stubby-rack   # godot-cpp submodule
scons -j$(nproc)                       # extension, template_debug, against api/extension_api.json
scons -j$(nproc) target=template_release
STUBBY_API=4.7 scons -j$(nproc)        # build against godot-cpp's bundled 4.7 API (CI baseline)
scons tests                            # core tests, no Godot needed
python3 tools/check_licences.py        # no copyleft in thirdparty/
godot --headless --path demo -s res://smoke_test.gd   # extension loads; prints SMOKE PASS
```

Refresh the API dump after updating Godot:
`cd api && godot --headless --dump-extension-api`.

Use in a game: copy `demo/addons/stubby_rack/` (the `.gdextension` file and `bin/`)
into the game's `addons/`.

## Known issue

On the Godot 4.8 beta (`e8fe0a1b5`), a *clean* headless `--import` of any project
with a GDExtension aborts on exit (exit 134) after importing. godot-cpp's own test
extension does the same, so it's the engine, not this extension. Later runs are
fine. Run the import twice, or ignore that first exit code.

## Licence

MIT. Third-party licences: THIRDPARTY.md.
