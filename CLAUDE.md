# CLAUDE.md — stubby-rack

The audio engine for the Godot game `wait-for-stubs` (`/home/buddha/src/wait-for-stubs`).
**Read the spec before changing behaviour:**
`../wait-for-stubs/openspec/changes/add-rack-audio-engine/` (design.md decisions
D1–D10, specs/, tasks.md). stubby-music's Rust engine (`../stubby-music/crates/stubby_audio`,
`spikes/spike_rack`) is the reference design and parity oracle, not a dependency.

## Rules

- **`src/core` never includes godot-cpp.** It must build and test with the plain test
  environment (`scons tests`). All Godot types live in `src/godot`.
- **Realtime safety on the audio thread** (anything reachable from `_mix` or rack
  rendering): no allocation or free, no locks, no I/O, no logging, no Godot object or
  script calls, no exceptions. Build modules off-thread and send them through the
  CHOC SPSC FIFO; return removed ones for freeing on the main thread.
- **VCV voltages** (`src/core/volts.h`): ±5 V audio, 10 V gates, Schmitt 0.1/1.0 V,
  1 V/oct from C4 (oscillators) or 2 Hz (clocks), NaN → 0, never clamp.
- **The rack's sample counter is the master clock** for all game audio. Godot doesn't
  expose its mix counter or mix callback to extensions, so nothing else can be.
- **DSP comes from existing libraries first**: DaisySP, Mutable Instruments STM32
  code, Signalsmith DSP, then Faust → C++. Write DSP by hand only when none fits, and
  say so in the module's definition.
- **Licences:** MIT, ISC, BSD, zlib and public domain only. Run
  `python3 tools/check_licences.py` after touching `thirdparty/`. Never fetch
  `DaisySP-LGPL` or define `USE_DAISYSP_LGPL`. Never copy from VCV Rack, Cardinal,
  MetaModule, Surge `sst-*` or Bespoke (GPL). Record every new dependency, with its
  pin, in THIRDPARTY.md.

## Commands

```bash
scons -j16                 # extension → demo/addons/stubby_rack/bin/
scons tests                # core tests (doctest), no Godot
python3 tools/check_licences.py
godot --headless --path demo -s res://smoke_test.gd
```

The first clean `--import` of `demo/` aborts with exit 134. That's a Godot 4.8 beta
bug that also hits godot-cpp's own test extension (README, *Known issue*). Run it
again.
