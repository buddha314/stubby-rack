extends SceneTree
## Headless check that the extension loads: godot --headless --path demo -s res://smoke_test.gd

func _init() -> void:
	var ok := Engine.has_singleton("RackServer")
	if ok:
		var rack: Object = Engine.get_singleton("RackServer")
		print("RackServer ", rack.get_version(), "; +1 V = ", rack.voct_to_hz(1.0), " Hz")
		ok = absf(rack.voct_to_hz(1.0) - 523.25) < 0.1
	print("SMOKE ", "PASS" if ok else "FAIL")
	quit(0 if ok else 1)
