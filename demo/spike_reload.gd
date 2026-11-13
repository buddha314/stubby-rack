extends SceneTree
## Spike 1.8: scene reload. Free every stem player and recreate them (what a scene
## change does) while the driver keeps running: does the clock keep going, do the new
## players join aligned, and does nothing crash?

func _initialize() -> void:
	_run.call_deferred()


func _spawn(n: int) -> Node3D:
	var world := Node3D.new()
	root.add_child(world)
	var camera := Camera3D.new()
	world.add_child(camera)
	camera.make_current()
	for i in n:
		var stem: AudioStream = ClassDB.instantiate("RackStemStream")
		stem.set("stem", i)
		var p := AudioStreamPlayer3D.new()
		p.stream = stem
		p.doppler_tracking = AudioStreamPlayer3D.DOPPLER_TRACKING_DISABLED
		world.add_child(p)
		p.play()
	return world


func _run() -> void:
	var server: Object = Engine.get_singleton("RackServer")
	server.spike_configure(440.0, 2.0, 32)
	AudioServer.set_bus_volume_db(0, -60.0)
	var driver := AudioStreamPlayer.new()
	driver.stream = ClassDB.instantiate("RackDriverStream")
	driver.process_mode = Node.PROCESS_MODE_ALWAYS
	root.add_child(driver)
	driver.play()
	await create_timer(0.5).timeout
	server.reset_spike_stats()
	var f0: int = server.get_frontier()
	for cycle in 20:
		var world := _spawn(32)
		await create_timer(0.25).timeout
		world.free()  # a scene change frees the players mid-mix-cycle
		await process_frame
	await create_timer(0.5).timeout
	var stats: Dictionary = server.get_spike_stats()
	print("RELOAD ", JSON.stringify({"cycles": 20, "clock_seconds": snappedf((server.get_frontier() - f0) / 48000.0, 0.01),
			"joins": stats.joins, "skips": stats.skips, "renders": stats.renders, "steps": stats.steps}))
	quit(0)
