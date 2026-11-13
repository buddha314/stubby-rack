# stubby-rack

The Godot-native modular-synth engine for
[wait-for-stubs](https://github.com/buddha314/wait-for-stubs): VCV Rack semantics
(every module once per sample, 1-sample cables, feedback allowed, VCV voltages)
inside Godot's own audio mix, as a C++ GDExtension built on godot-cpp. It also owns
the game's **master clock**: one sample counter and transport that time every sound in
the game.

**Status:** engine core started (task group 3): the per-sample rack, polyphony-ready
cables, the lock-free command/event boundary and an allocation guard. A 200-module
patch renders a 512-frame block in about 0.6 ms (5.6%). Earlier, the hosting spike
(task group 1) was done. The extension loads in
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

## Engine core (`src/core`)

- **`Rack`** (audio thread): every module runs once per sample. Outputs are
  double-buffered `PolyVoltage`s (1 to 16 channels), so every cable is a 1-sample
  delay, module order never matters, and feedback is legal. Capacities are fixed at
  construction.
- **`Engine`**: the boundary.
  - The main-thread controller validates commands (add/remove module, connect,
    disconnect, set a parameter, assign a stem) against its own bookkeeping, resolves
    module IDs to port slots, and rejects bad commands as events naming them.
  - `commit()` sends a frame's commands as one batch through a CHOC lock-free FIFO.
  - `render(frames)` (audio thread) applies only complete batches, so a frame's edits
    land on one sample, then renders and advances the master sample counter.
  - Removed modules come back through a return FIFO and are deleted on the main thread
    (`collect()`).
- **Master clock** (`transport.h`, `modules/clocks.h`, `sync.h`): the sample counter is
  the time base.
  - `Transport` holds tempo (1 BPM by default), meter and running state. Position is
    piecewise linear in samples: a tempo, start/stop or sample-rate change re-anchors at
    the current sample, so position never jumps and a device change keeps the bar.
  - `Engine::at(When)` schedules the commands that follow on an exact sample, or on the
    next beat, bar or beat division. Quantized times are kept in beats, so tempo and rate
    changes keep them on their beat. Render splits the block at each action. An action
    whose time has already passed applies at the block start with a `Late` event.
  - `TempoClock(N)` ticks at whole multiples of the master. Tick k is computed exactly
    from the anchor (never accumulated), so ×90 and ×120 at 1 BPM still meet on every
    master beat after an hour. `RandomClock` is the seeded, free-running bug clock. Each
    tick is an event carrying its exact sample.
  - `set_paused` fades the emitter stems out over 30 ms; the clock and the UI stem (the
    last stem) run on. `position()` and `predict_ns()` (the sync point) are lock-free reads.
- **`modules/basic.h`**: first versions of Clock, Envelope, VCO (DaisySP), VCA, Mix4,
  Constant and Thru.
- **`patches.h`**: the 200-module reference patch (stubby-music's `spike_rack`).
- **Tests:** the spec's scenarios, and an allocation guard (operator new is replaced
  in the test binary): 1,000 edited blocks allocate nothing on the render thread.
  The budget test is 200 modules in under 1.6 ms. `test_clock.cpp` covers the master-clock spec scenarios.

Don't build with `-march=native`: combined with `-O3`, it made the engine about twice
as slow.

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
