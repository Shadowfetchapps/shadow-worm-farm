extends SceneTree
## Checks that going live refuses a server address that is not an RTMP URL (a share link, a source name, blank).
## godot --headless --path game --script res://tests/live_server_check.gd

func _init() -> void:
	var ok := true
	for case in [["x", "https://x.com/i/broadcasts/abc", false], ["x", "Source 1", false], ["custom", "", false]]:
		var live := LiveStream.new()
		live.setup(null, {"live": {"destination": case[0], "servers": {case[0]: case[1]}, "quality": 0, "auto": false}})
		var started := live.start()
		var st := live.status()
		var good: bool = started == case[2] and String(st.get("state", "")) == "failed" and String(st.get("message", "")).contains("rtmp://")
		print("%s %-34s -> started %s, %s: %s" % ["ok  " if good else "FAIL", "'%s'" % case[1], started, st.get("state", ""), st.get("message", "")])
		ok = ok and good
		live.free()
	quit(0 if ok else 1)
