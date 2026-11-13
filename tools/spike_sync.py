"""Spike 1.6: clock-to-light analysis of a spike_clock.gd sync log.

The sync point maps samples to wall time. Godot's audio thread renders on demand, and
PulseAudio asks for two 512-frame steps back to back, so a raw render time can be up to
a block early. Compare the raw mapping with a just-in-time estimator (the upper envelope
of render time minus sample time over a sliding window), then the error of flashing a
light on the frame nearest each predicted tick.
Usage: python3 tools/spike_sync.py log.json
"""
import json
import statistics
import sys

SR = 48000.0


def pct(values, p):
    s = sorted(values)
    return s[min(len(s) - 1, int(len(s) * p))]


def main(path):
    d = json.load(open(path))
    sync = d["sync"]
    samples = sync[0::2]
    ns = sync[1::2]
    # Offsets: wall time minus the time the sample "should" be due, ms.
    off = [(t - s / SR * 1e9) / 1e6 for s, t in zip(samples, ns)]
    n = len(off)
    # Remove the device clock's drift (ppm) with a least-squares line.
    xs = [s / SR for s in samples]
    mx, my = statistics.fmean(xs), statistics.fmean(off)
    slope = sum((x - mx) * (y - my) for x, y in zip(xs, off)) / sum((x - mx) ** 2 for x in xs)
    raw = [y - (my + slope * (x - mx)) for x, y in zip(xs, off)]
    # Just-in-time: the upper envelope over the last 16 renders (~170 ms).
    jit = []
    for i in range(n):
        lo = max(0, i - 15)
        jit.append(max(off[lo:i + 1]))
    jit_res = [y - (my + slope * (x - mx)) for x, y in zip(xs, jit)]
    jmean = statistics.fmean(jit_res)
    jit_res = [r - jmean for r in jit_res]
    print(f"renders {n}, span {xs[-1] - xs[0]:.1f} s, device clock vs wall {slope * 1000:.0f} ppm, "
          f"output latency {d['output_latency_s'] * 1000:.1f} ms, driver {d['driver']}")
    print(f"raw sync point: |error| p50 {pct([abs(r) for r in raw], .5):.2f} ms, p99 {pct([abs(r) for r in raw], .99):.2f} ms, max {max(abs(r) for r in raw):.2f} ms")
    print(f"just-in-time:   |error| p50 {pct([abs(r) for r in jit_res], .5):.2f} ms, p99 {pct([abs(r) for r in jit_res], .99):.2f} ms, max {max(abs(r) for r in jit_res):.2f} ms")
    # Lights: a tick every 0.5 s; flash on the frame nearest its predicted audible instant
    # (just-in-time mapping + output latency). Error = |frame time - predicted instant|.
    frames = d["frames_ns"]
    lat_ns = d["output_latency_s"] * 1e9
    errs = []
    fi = 0
    tick = samples[20]
    while tick < samples[-20]:
        # Nearest render at or before the tick.
        lo, hi = 0, n - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if samples[mid] <= tick: lo = mid
            else: hi = mid - 1
        predicted = tick / SR * 1e9 + jit[lo] * 1e6 + lat_ns
        while fi + 1 < len(frames) and abs(frames[fi + 1] - predicted) <= abs(frames[fi] - predicted):
            fi += 1
        errs.append(abs(frames[fi] - predicted) / 1e6)
        tick += int(SR / 2)
    print(f"light on nearest frame ({len(errs)} ticks): |error| p50 {pct(errs, .5):.2f} ms, p99 {pct(errs, .99):.2f} ms, max {max(errs):.2f} ms "
          f"(frame interval {statistics.fmean([b - a for a, b in zip(frames, frames[1:])]) / 1e6:.2f} ms)")


if __name__ == "__main__":
    main(sys.argv[1])
