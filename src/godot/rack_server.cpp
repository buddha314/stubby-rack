#include "rack_server.h"

#include <godot_cpp/core/class_db.hpp>

#include "core/version.h"
#include "core/volts.h"

namespace godot {

RackServer *RackServer::singleton = nullptr;

RackServer::RackServer() { singleton = this; }
RackServer::~RackServer() {
    if (singleton == this) singleton = nullptr;
}

String RackServer::get_version() const { return String(stubby::kVersion); }

double RackServer::voct_to_hz(double volts) const {
    return stubby::volts::voct_to_hz(static_cast<float>(volts));
}

void RackServer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_version"), &RackServer::get_version);
    ClassDB::bind_method(D_METHOD("voct_to_hz", "volts"), &RackServer::voct_to_hz);
}

}  // namespace godot
