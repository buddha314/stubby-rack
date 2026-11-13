extends SceneTree
## Spike 1.2-1.4 (wait-for-stubs add-rack-audio-engine D4): one shared tone fanned out
## to N AudioStreamPlayer3D stems, plus the always-on driver. Half the stems start
## two seconds in, to exercise mid-game joins. Prints one JSON line "SPIKE {...}".
##   godot --headless --path demo -s res://spike_fanout.gd -- --emitters=48 --seconds=600
##   (windowed: real audio driver; --volume-db=-60 keeps it quiet)

var emitters := 48
var seconds := 10.0
var volume_db := 0.0
var rack_modules := 0


func _initialize() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--emitters="): emitters = arg.trim_prefix("--emitters=").to_int()
		elif arg.begins_with("--seconds="): seconds = arg.trim_prefix("--seconds=").to_float()
		elif arg.begins_with("--volume-db="): volume_db = arg.trim_prefix("--volume-db=").to_float()
		elif arg.begins_with("--rack="): rack_modules = arg.trim_prefix("--rack=").to_int()
	_run.call_deferred()


func _run() -> void:
	var server: Object = Engine.get_singleton("RackServer")
	AudioServer.set_bus_volume_db(0, volume_db)
	server.spike_configure(440.0, 2.0, maxi(emitters, 1))
	var built: int = server.spike_use_rack(rack_modules)
	var world := Node3D.new()
	root.add_child(world)
	var camera := Camera3D.new()
	world.add_child(camera)
	camera.make_current()

	var driver := AudioStreamPlayer.new()
	driver.stream = ClassDB.instantiate("RackDriverStream")
	driver.process_mode = Node.PROCESS_MODE_ALWAYS
	world.add_child(driver)
	driver.play()

	var players: Array[AudioStreamPlayer3D] = []
	for i in emitters:
		var stem: AudioStream = ClassDB.instantiate("RackStemStream")
		stem.set("stem", i)
		var p := AudioStreamPlayer3D.new()
		p.stream = stem
		p.doppler_tracking = AudioStreamPlayer3D.DOPPLER_TRACKING_DISABLED
		p.max_distance = 40.0
		var angle := TAU * i / maxi(emitters, 1)
		p.position = Vector3(cos(angle), 0.0, sin(angle)) * (5.0 + 10.0 * (i % 3))
		world.add_child(p)
		players.append(p)
	# First half now, second half after two seconds (mid-game joins).
	for i in players.size() / 2:
		players[i].play()
	await create_timer(2.0).timeout
	for i in range(players.size() / 2, players.size()):
		players[i].play()
	await create_timer(1.0).timeout
	server.reset_spike_stats()

	var frontier0: int = server.get_frontier()
	var t0 := Time.get_ticks_usec()
	var frames := 0
	var process_us := 0.0
	while (Time.get_ticks_usec() - t0) / 1e6 < seconds:
		await process_frame
		frames += 1
		process_us += Performance.get_monitor(Performance.TIME_PROCESS) * 1e6
	var wall := (Time.get_ticks_usec() - t0) / 1e6
	var stats: Dictionary = server.get_spike_stats()
	stats["emitters"] = emitters
	stats["rack_modules"] = built
	stats["seconds"] = snappedf(wall, 0.01)
	stats["rendered_seconds"] = snappedf((server.get_frontier() - frontier0) / 48000.0, 0.01)
	stats["clock_drift_ms"] = snappedf(((server.get_frontier() - frontier0) / 48000.0 - wall) * 1000.0, 0.1)
	stats["mix_rate"] = AudioServer.get_mix_rate()
	stats["driver"] = AudioServer.get_driver_name() if AudioServer.has_method("get_driver_name") else ""
	stats["main_process_ms_per_frame"] = snappedf(process_us / maxi(frames, 1) / 1000.0, 0.001)
	stats["fps"] = snappedf(frames / wall, 0.1)
	stats["playing"] = players.filter(func(p: AudioStreamPlayer3D) -> bool: return p.playing).size()
	print("SPIKE ", JSON.stringify(stats))
	if OS.get_cmdline_user_args().has("--trace"):
		var trace: Array = server.get_trace()
		var base: int = trace[0][4]
		for row in trace.slice(trace.size() - 40):
			print("TRACE stem=%3d pos=%9d front=%9d frames=%d t_us=%.1f" % [row[0], row[1], row[2], row[3], (row[4] - base) / 1000.0])
	quit(0)
