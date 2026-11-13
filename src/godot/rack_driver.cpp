#include "rack_driver.h"

#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/project_settings.hpp>

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

    // The UI stem: non-spatial, never muted by pause (the sampler's one-shots land here).
    // Its bus is the project setting audio/stubby_rack/ui_bus (default Master).
    RackServer *server = RackServer::get_singleton();
    AudioStreamPlayer *ui = memnew(AudioStreamPlayer);
    ui->set_name("UiPlayer");
    ui->set_process_mode(PROCESS_MODE_ALWAYS);
    Ref<RackStemStream> ui_stream;
    ui_stream.instantiate();
    ui_stream->set_stem(server ? server->get_ui_stem() : -1);
    ui->set_stream(ui_stream);
    ProjectSettings *ps = ProjectSettings::get_singleton();
    if (ps && ps->has_setting("audio/stubby_rack/ui_bus")) ui->set_bus(ps->get_setting("audio/stubby_rack/ui_bus"));
    add_child(ui);
    ui->play();
    set_process(true);
}

void RackDriver::_process(double) {
    if (RackServer *server = RackServer::get_singleton()) server->frame_update();
}

}  // namespace godot
