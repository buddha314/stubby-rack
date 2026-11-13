#include "rack_driver.h"

#include <godot_cpp/classes/audio_stream_player.hpp>

#include "rack_server.h"
#include "rack_streams.h"

namespace godot {

RackDriver::RackDriver() {
    set_process_mode(PROCESS_MODE_ALWAYS);
    set_process_priority(1 << 20);  // after game scripts: their edits land this frame
}

void RackDriver::_ready() {
    AudioStreamPlayer *player = memnew(AudioStreamPlayer);
    player->set_name("DriverPlayer");
    player->set_process_mode(PROCESS_MODE_ALWAYS);
    Ref<RackDriverStream> stream;
    stream.instantiate();
    player->set_stream(stream);
    add_child(player);
    player->play();
    set_process(true);
}

void RackDriver::_process(double) {
    if (RackServer *server = RackServer::get_singleton()) server->frame_update();
}

}  // namespace godot
