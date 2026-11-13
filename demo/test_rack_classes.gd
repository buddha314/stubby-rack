extends SceneTree
## Group 5 (Godot-facing classes) end to end, headless (the Dummy audio driver still
## mixes, at roughly real-time pace):
##   godot --headless --path demo -s res://test_rack_classes.gd
## RackServer reads the project's clock settings, a Rack instantiates a RackPatch, a
## RackEmitter plays its stem, ticks arrive as signals with exact samples, the sampler
## plays a one-shot on its sample, pause keeps the clock, patches round-trip, and
## definitions agree with the core.

var _failures: PackedStringArray = []
var _ticks: Array[Vector2i] = []  # (module id, sample)
var _late := 0
var _rejected: PackedStringArray = []


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var server: Object = Engine.get_singleton("RackServer")
	server.clock_ticked.connect(func(id: int, sample: int, _index: int) -> void: _ticks.append(Vector2i(id, sample)))
	server.action_late.connect(func(_a: int, _missed: int, _s: int) -> void: _late += 1)
	server.command_rejected.connect(func(_id: int, reason: String) -> void: _rejected.append(reason))

	# Project settings are the defaults (the demo's meter is 3, the game's 4).
	var pos: Dictionary = server.get_position()
	_expect(pos.bpm == 1.0 and pos.beats_per_bar == 3, "tempo and meter from project settings (got %s)" % pos)
	var rate: float = server.get_sample_rate()
	_expect(rate == 48000.0, "48 kHz from audio/driver/mix_rate")

	# Definitions agree with the core; a wrong one says how.
	for kind: Dictionary in server.get_module_kinds():
		var def := RackModuleDefinition.from_kind(kind.kind)
		_expect(def != null and def.check_against_core().is_empty(), "definition of %s agrees" % kind.kind)
	var bad := RackModuleDefinition.from_kind("vco_square")
	bad.inputs = PackedStringArray(["pitch"])
	_expect(bad.check_against_core().size() == 2, "a wrong definition lists its mismatches: %s" % bad.check_against_core())

	# A patch: a ×120 clock gating a square VCO through a VCA, heard from an emitter.
	var patch := RackPatch.new()
	patch.modules = [
		{"place": "clock", "kind": "tempo_clock", "params": {"multiple": 120}},
		{"place": "osc", "definition": RackModuleDefinition.from_kind("vco_square"), "params": {"freq": 110.0}},
		{"place": "vca", "kind": "vca"},
	]
	patch.cables = [
		{"from": "osc", "output": "out", "to": "vca", "input": "in"},
		{"from": "clock", "output": 0, "to": "vca", "input": "cv"},
	]
	patch.emitters = [{"module": "vca", "output": "out", "position": Vector3(1, 0, 0), "max_distance": 30.0}]
	var rack := Rack.new()
	rack.patch = patch
	root.add_child(rack)
	await process_frame
	await process_frame
	var emitter := rack.get_node_or_null("Emitter0") as RackEmitter
	_expect(rack.is_instantiated(), "the rack instantiated its patch")
	_expect(emitter != null and emitter.get_stem() >= 0, "the patch's emitter bound a stem")
	_expect(root.get_node_or_null("RackDriver") != null, "RackServer added its driver to the tree")
	_expect(emitter != null and emitter.doppler_tracking == AudioStreamPlayer3D.DOPPLER_TRACKING_DISABLED
			and emitter.process_mode == Node.PROCESS_MODE_ALWAYS, "emitter: doppler off, process always")

	var start: int = server.now_sample()
	await create_timer(1.5).timeout
	var advanced: int = server.now_sample() - start
	_expect(advanced > 24000, "the master clock advanced (%d samples in 1.5 s)" % advanced)
	_expect(emitter != null and emitter.playing, "the emitter is playing")
	var clock_id := rack.get_module_id("clock")
	var mine := _ticks.filter(func(t: Vector2i) -> bool: return t.x == clock_id)
	_expect(mine.size() >= 2, "clock ticks arrive as signals (%d)" % mine.size())
	var spacing := int(rate * 60.0 / 120.0)
	_expect(mine.all(func(t: Vector2i) -> bool: return t.y % spacing == 0), "every ×120 tick is on a 1/120 boundary")
	_expect(server.sample_to_ticks_usec(server.now_sample()) > 0, "the sync point maps samples to wall time")

	# A one-shot on an exact sample, through the UI stem's sampler.
	var wav := AudioStreamWAV.new()
	wav.format = AudioStreamWAV.FORMAT_16_BITS
	wav.mix_rate = 48000
	var data := PackedByteArray()
	data.resize(960 * 2)
	for i in 960:
		data.encode_s16(i * 2, 16000 if i < 480 else -16000)
	wav.data = data
	var sample_id: int = server.load_sample(wav)
	_expect(sample_id >= 0 and server.load_sample(wav) == sample_id, "a sample loads once (cached)")
	server.play_sample(sample_id, RackServer.WHEN_SAMPLE, server.now_sample() + 9600)
	await create_timer(0.5).timeout
	_expect(_late == 0, "a one-shot 200 ms ahead is not late")

	# Pause: the clock keeps time.
	paused = true
	server.set_paused(true)
	var before: int = server.now_sample()
	var ticks_before := _ticks.size()
	await create_timer(0.6, true).timeout
	_expect(server.now_sample() > before + 12000, "paused, the clock runs on")
	_expect(_ticks.size() > ticks_before, "paused, clock ticks still arrive")
	paused = false
	server.set_paused(false)

	# Patch round trip: live state → RackPatch → dictionary → RackPatch → a second rack.
	rack.set_param("osc", "freq", 220.0)
	var captured := rack.to_patch()
	var again := RackPatch.from_dictionary(captured.to_dictionary())
	_expect(again.cables.size() == 2 and again.modules.size() == 3 and again.emitters.size() == 1, "patch round-trips")
	var twin := Rack.new()
	twin.patch = again
	root.add_child(twin)
	await process_frame
	_expect(twin.is_instantiated() and is_equal_approx(twin.get_param("osc", "freq"), 220.0), "a twin rack has the edited value")
	twin.queue_free()

	# Bad commands come back as signals.
	server.add_module(server.new_module_id(), "no_such_kind")
	_expect(_rejected.size() == 1, "an unknown kind is rejected with a reason: %s" % [_rejected])

	rack.queue_free()
	await process_frame
	await process_frame
	var stats: Dictionary = server.get_driver_stats()
	print("driver stats: ", stats)
	_expect(stats.renders > 50, "the driver rendered blocks")

	if _failures.is_empty():
		print("test_rack_classes: OK")
		quit(0)
	else:
		for f in _failures:
			printerr("FAIL: ", f)
		quit(1)


func _expect(ok: bool, what: String) -> void:
	if not ok:
		_failures.append(what)
