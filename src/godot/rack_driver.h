#pragma once
// RackDriver: the node that keeps the engine running (design D4, D5). RackServer adds
// one to the scene root on first use. It holds the always-on silent player (the
// driver playback that renders blocks when nothing is audible) and the UI stem's
// non-spatial player (bus: audio/stubby_rack/ui_bus), and once per frame,
// late in the frame, commits the frame's commands and emits engine events. Both run
// with process_mode ALWAYS, so pausing the game never stops the master clock.

#include <godot_cpp/classes/node.hpp>

namespace godot {

class RackDriver : public Node {
    GDCLASS(RackDriver, Node)
public:
    RackDriver();
    void _ready() override;
    void _process(double delta) override;
protected:
    static void _bind_methods() {}
};

}  // namespace godot
