# stubby-rack

The Godot-native modular-synth engine for
[wait-for-stubs](https://github.com/buddha314/wait-for-stubs): VCV Rack semantics
(every module once per sample, 1-sample cables, feedback allowed, VCV voltages)
inside Godot's own audio mix, as a C++ GDExtension built on godot-cpp. It also owns
the game's **master clock**: one sample counter and transport that time every sound in
the game.

**Status:** hosting spike done (task group 1 of the spec). The extension loads in
Godot 4.8. A driver player and per-emitter stem players fan one rendered block out to
`AudioStreamPlayer3D`s, sample-aligned. A port of stubby-music's 200-module feedback
rack renders inside `_mix` within budget. Results are in the spec's design.md
("Spike results"). The engine itself (scheduler, clock, modules) is next.

## Hosting: how the rack reaches Godot's mix

Godot calls every playing stream's `_mix` once per 512-frame step on its audio thread,
in an order we don't control, and gives extensions no step counter. So:
- **`RackDriverStream`** plays on an always-on, silent `AudioStreamPlayer` with
  `process_mode = ALWAYS`, so the rack and its clock run even when nothing is audible
  or the game is paused (a `PAUSABLE` driver stops the clock).
- **`RackStemStream`** plays one emitter's stem on an `AudioStreamPlayer3D`. It isn't
  resampled, so doppler and `pitch_scale` don't apply.
- **`StemRenderer`** (`src/core`) detects mix steps itself. A new step begins when the
  gap since the end of the previous read exceeds a quarter block, or when a reader that
  already read in this step reads again (PulseAudio asks for two steps back to back).
  The first read of a step renders the block, and every read in the step gets that
  same block, so all emitters stay sample-aligned. Its sample frontier is the master
  clock.

Spike tools (`demo/spike_*.gd`, `tools/spike_sync.py`, `scons bench`):

```bash
godot --path demo -s res://spike_fanout.gd -- --emitters=48 --seconds=60 --volume-db=-60 [--rack=200]
godot --path demo -s res://spike_clock.gd -- --mode=sync --seconds=60 --out=/tmp/sync.json
python3 tools/spike_sync.py /tmp/sync.json
godot --path demo -s res://spike_clock.gd -- --mode=pause
godot --path demo -s res://spike_reload.gd
scons bench && ./build/bench/spike_rack_bench
```

Use a window (not `--headless`) for timing: headless Godot uses the Dummy audio
driver, which doesn't keep real-time pace.

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
