extends SceneTree
## Spike 1.6 and 1.7 (wait-for-stubs add-rack-audio-engine D5):
##  sync  - log every render's (first sample, wall ns) and the main thread's frame times,
##          for the clock-to-light analysis (tools/spike_sync.py).
##  pause - pause the tree with the driver at PROCESS_MODE_ALWAYS and at PAUSABLE: does
##          the clock keep running?
##   godot --path demo -s res://spike_clock.gd -- --mode=sync --seconds=60 --out=/tmp/x.json

var mode := "sync"
var seconds := 30.0
var out_path := ""


func _initialize() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--mode="): mode = arg.trim_prefix("--mode=")
		elif arg.begins_with("--seconds="): seconds = arg.trim_prefix("--seconds=").to_float()
		elif arg.begins_with("--out="): out_path = arg.trim_prefix("--out=")
	_run.call_deferred()


func _driver(process_mode_value: int) -> AudioStreamPlayer:
	var driver := AudioStreamPlayer.new()
	driver.stream = ClassDB.instantiate("RackDriverStream")
	driver.process_mode = process_mode_value
	root.add_child(driver)
	driver.play()
	return driver


func _run() -> void:
	var server: Object = Engine.get_singleton("RackServer")
	AudioServer.set_bus_volume_db(0, -60.0)
	if mode == "pause":
		for pm in [Node.PROCESS_MODE_ALWAYS, Node.PROCESS_MODE_PAUSABLE]:
			var driver := _driver(pm)
			await create_timer(1.0).timeout
			var before: int = server.get_frontier()
			var t0 := Time.get_ticks_usec()
			paused = true
			await create_timer(3.0, true).timeout  # process_always timer runs while paused
			var during: int = server.get_frontier()
			paused = false
			var wall := (Time.get_ticks_usec() - t0) / 1e6
			print("PAUSE ", JSON.stringify({"process_mode": "ALWAYS" if pm == Node.PROCESS_MODE_ALWAYS else "PAUSABLE",
					"paused_seconds": snappedf(wall, 0.01), "clock_advanced_seconds": snappedf((during - before) / 48000.0, 0.01)}))
			driver.queue_free()
			await create_timer(0.5).timeout
		quit(0)
		return
	_driver(Node.PROCESS_MODE_ALWAYS)
	await create_timer(1.0).timeout
	server.reset_spike_stats()
	var frames := PackedInt64Array()
	var t0 := Time.get_ticks_usec()
	while (Time.get_ticks_usec() - t0) / 1e6 < seconds:
		await process_frame
		frames.append(server.now_ns())  # the sync points' time base
	var result := {"sync": Array(server.get_sync_log()), "frames_ns": Array(frames),
			"output_latency_s": AudioServer.get_output_latency(), "driver": AudioServer.get_driver_name()}
	var file := FileAccess.open(out_path, FileAccess.WRITE)
	file.store_string(JSON.stringify(result))
	print("SYNC written %d renders, %d frames, latency %.1f ms" % [result.sync.size() / 2, frames.size(), result.output_latency_s * 1000.0])
	quit(0)
