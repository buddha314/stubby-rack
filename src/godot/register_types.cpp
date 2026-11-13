#include <gdextension_interface.h>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "rack_server.h"

using namespace godot;

static RackServer *rack_server = nullptr;

static void initialize_stubby_rack(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    GDREGISTER_CLASS(RackServer);
    rack_server = memnew(RackServer);
    Engine::get_singleton()->register_singleton("RackServer", rack_server);
}

static void uninitialize_stubby_rack(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    Engine::get_singleton()->unregister_singleton("RackServer");
    memdelete(rack_server);
    rack_server = nullptr;
}

extern "C" {
GDExtensionBool GDE_EXPORT stubby_rack_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
        GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
    GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(initialize_stubby_rack);
    init_obj.register_terminator(uninitialize_stubby_rack);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}
}
