# Third-party code

Everything vendored here is permissively licensed; the game (wait-for-stubs) is MIT,
so GPL and LGPL code must never be linked in. `python3 tools/check_licences.py`
enforces that. Copies are trimmed to what the engine uses. Update by re-fetching the
pinned revision and re-copying the same paths.

| Library | Licence | Pin | Vendored paths | Used for |
| --- | --- | --- | --- | --- |
| [godot-cpp](https://github.com/godotengine/godot-cpp) | MIT | `10.0.0-stable` (`507ed9d8`), git submodule | `godot-cpp/` | GDExtension bindings |
| [DaisySP](https://github.com/daisyaudio/DaisySP) | MIT | `2c72eaf9ea` | `thirdparty/daisysp/Source/` | oscillators, filters, envelopes, noise, delay, physical models |
| [Mutable Instruments eurorack](https://github.com/pichenettes/eurorack) | MIT (STM32 code) | `f4cfa76970` | `thirdparty/mutable/{plaits,rings,clouds}/{dsp,resources.*}` | Plaits, Rings, Clouds voices |
| [stmlib](https://github.com/pichenettes/stmlib) | MIT (`murmurhash3.h`: public domain) | `e3bd7c9cc0` (eurorack's submodule pin) | `thirdparty/mutable/stmlib/{dsp,utils,fft,stmlib.h}` | support code for the MI modules |
| [CHOC](https://github.com/Tracktion/choc) | ISC | `8af031c2ba` | `thirdparty/choc/choc/…` (23 headers: FIFOs, SmallVector, sample buffers, block dispatcher, WAV) | lock-free SPSC queues, splitting blocks at event offsets, WAV loading for the sampler |
| [Signalsmith DSP](https://github.com/Signalsmith-Audio/dsp) | MIT | `v1.7.3` | `thirdparty/signalsmith/` | delays, filters, FFT, oversampling |
| [doctest](https://github.com/doctest/doctest) | MIT | `v2.5.3` | `thirdparty/doctest/doctest.h` | core tests only (not linked into the extension) |

## Deliberately excluded

- **`DaisySP-LGPL`** (a submodule of DaisySP): never fetched. `daisysp.h` only
  includes it when `USE_DAISYSP_LGPL` is defined, and the licence check fails if
  anything defines it.
- **Mutable Instruments' AVR projects** (GPL-3) and `stmlib/third_party`.
- **GPL-3 engines with VCV semantics**: VCV Rack, Cardinal, 4ms MetaModule, Bespoke.
  Read them for *behaviour* only; never copy code.
- **Surge `sst-*` libraries** (GPL-3, apart from a few individually marked headers),
  **Tracktion Engine** and the **Cmajor runtime** (GPL or commercial).
- Anything without a licence (GodotConductor, modular_godot, metamodule-core-interface).

## Why the rest is written here

A 2026-11-13 survey (recorded in wait-for-stubs, `docs/research/rack-engine-reuse.md`)
found no permissively licensed per-sample modular scheduler, sample-accurate master
clock or one-engine-to-many-AudioStreamPlayer3D fan-out. Permissive graph engines
(miniaudio, Elementary, LabSound) process in blocks, so feedback costs a block.
Godot's `AudioStreamInteractive`, `AudioStreamSynchronized` and
`AudioStreamPolyphonic` have no shared clock that other code can schedule against.
