#pragma once
// RackServer: the engine singleton scripts talk to (design D6). For now it only
// reports its version and exposes the core's voltage helpers, which proves the
// extension loads and the core links.

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

class RackServer : public Object {
    GDCLASS(RackServer, Object)

public:
    static RackServer *get_singleton() { return singleton; }
    RackServer();
    ~RackServer() override;

    String get_version() const;
    double voct_to_hz(double volts) const;

protected:
    static void _bind_methods();

private:
    static RackServer *singleton;
};

}  // namespace godot
